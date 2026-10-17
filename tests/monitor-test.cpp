#include "monitor.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>
#include <cerrno>

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

static bool fixture(const QString &directory)
{
    const auto path = directory + "/opencode";
    if (!writeFile(path, R"PY(#!/usr/bin/env python3
import json, sys, time, os
from pathlib import Path
base = Path(__file__).parent
mode = (base / 'mode').read_text() if (base / 'mode').exists() else ''
path = sys.argv[3]
with (base / 'calls').open('a') as log: log.write(path + '\n')
if mode == 'hang' or (mode == 'hangActive' and path.endswith('/active')):
    (base / 'pid').write_text(str(os.getpid()))
    time.sleep(60)
if mode == 'error': sys.exit(1)
root = {'id': 'root', 'time': {'updated': 20}, 'title': 'Root'}
child = {'id': 'child', 'parentID': 'root', 'time': {'updated': 10}, 'outcome': 'succeeded'}
if mode == 'change': root.update(title='Changed', time={'updated': 30})
if mode.startswith('archive'): root['time']['archived'] = 1
if path.endswith('/active'):
    result = {'data': {'root': {'type': 'running'}} if mode == 'archiveRunning' else {}}
elif 'cursor=' in path:
    result = {'data': [], 'cursor': {}}
else:
    result = {'data': [root] if mode == 'delete' else [root, child], 'cursor': {'next': 'end'}}
print(json.dumps(result))
)PY")) return false;
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

class MonitorTest : public QObject
{
    Q_OBJECT
private slots:
    void missingExecutable()
    {
        Monitor monitor;
        monitor.setProperty("executable", "/nonexistent/opencode");
        monitor.setEnabled(true);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(!monitor.error().isEmpty());
        QVERIFY(monitor.snapshot().isEmpty());
    }

    void paginationAndRecovery()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("opencode");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"SH(#!/usr/bin/env python3
import json, sys
assert sys.argv[1:3] == ['api', 'get']
path = sys.argv[3]
if path == '/api/session?limit=1000':
    result = {'data': [{'id': 'root'}], 'cursor': {'next': 'page2'}}
elif 'cursor=page2' in path:
    result = {'data': [], 'cursor': {}}
elif path == '/api/session/active':
    result = {'data': {'child': {'type': 'running'}}}
elif path == '/api/session/child':
    result = {'data': {'id': 'child', 'parentID': 'root'}}
else:
    sys.exit(1)
print(json.dumps(result))
)SH");
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        Monitor monitor;
        monitor.setProperty("executable", path);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotChanged);
        monitor.setEnabled(true);
        monitor.refresh(); // Overlapping polls must not start another process.
        QTRY_COMPARE(snapshots.count(), 1);
        QVERIFY(monitor.error().isEmpty());
        QCOMPARE(monitor.snapshot().value("sessions").toList().size(), 2);
        QVERIFY(monitor.snapshot().value("active").toMap().contains("child"));
        const auto previous = monitor.snapshot();

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("#!/bin/sh\nprintf 'not json'\n");
        file.close();
        monitor.refresh();
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(!monitor.error().isEmpty());
        QCOMPARE(monitor.snapshot(), previous);
        QCOMPARE(snapshots.count(), 1);

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("#!/bin/sh\nprintf '%s' '{\"data\":[],\"cursor\":{}}'\n");
        file.close();
        // A wrong active-response shape must not replace the retained snapshot.
        monitor.refresh();
        QTRY_VERIFY(!monitor.busy());
        QCOMPARE(monitor.snapshot(), previous);

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"SH(#!/usr/bin/env python3
import json, sys
print(json.dumps({'data': {}} if sys.argv[3] == '/api/session/active' else {'data': [], 'cursor': {}}))
)SH");
        file.close();
        monitor.refresh();
        QTRY_COMPARE(snapshots.count(), 2);
        QVERIFY(monitor.error().isEmpty());
        QVERIFY(monitor.snapshot().value("sessions").toList().isEmpty());
    }

    void intervalBounds()
    {
        Monitor monitor;
        monitor.setInterval(-1);
        QCOMPARE(monitor.interval(), 10);
        monitor.setInterval(10000);
        QCOMPARE(monitor.interval(), 3600);
    }

    void incrementalAndReconciliation()
    {
        QTemporaryDir directory;
        QVERIFY(fixture(directory.path()));
        Monitor monitor;
        monitor.setProperty("executable", directory.filePath("opencode"));
        QSignalSpy snapshots(&monitor, &Monitor::snapshotChanged);
        monitor.setEnabled(true);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.error().isEmpty());
        QCOMPARE(snapshots.count(), 1);
        QVERIFY(writeFile(directory.filePath("calls"), {}));
        const auto updated = monitor.updatedAt();
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.updatedAt() >= updated);
        QCOMPARE(snapshots.count(), 1); // No UI invalidation on unchanged history.
        QCOMPARE(readFile(directory.filePath("calls")), QByteArray("/api/session?limit=100\n/api/session/active\n"));
        QVERIFY(writeFile(directory.filePath("mode"), "change"));
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QCOMPARE(snapshots.count(), 2);
        QCOMPARE(monitor.m_cache.value("root").value("title").toString(), "Changed");
        QVERIFY(writeFile(directory.filePath("mode"), "delete"));
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QCOMPARE(monitor.m_cache.size(), 2); // Incremental API has no deletion tombstones.
        monitor.refresh();
        QTRY_VERIFY(!monitor.busy());
        QCOMPARE(monitor.m_cache.size(), 1);
        QVERIFY(!monitor.m_cache.contains("child"));
        QVERIFY(writeFile(directory.filePath("mode"), "archive"));
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(!monitor.m_cache.contains("root"));
        QVERIFY(writeFile(directory.filePath("mode"), "archiveRunning"));
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.m_cache.contains("root"));
        QVERIFY(monitor.snapshot().value("active").toMap().contains("root"));
    }

    void pauseCancellationTimeoutAndResume()
    {
        QTemporaryDir directory;
        QVERIFY(fixture(directory.path()));
        Monitor monitor;
        monitor.setProperty("executable", directory.filePath("opencode"));
        QSignalSpy snapshots(&monitor, &Monitor::snapshotChanged);
        QVERIFY(!monitor.enabled());
        monitor.refresh();
        QVERIFY(!monitor.busy());
        QVERIFY(!monitor.m_timer.isActive());
        QVERIFY(!QFile::exists(directory.filePath("calls")));
        monitor.setEnabled(true);
        QTRY_VERIFY(!monitor.busy());
        const auto previous = monitor.snapshot();
        const auto updated = monitor.updatedAt();
        QVERIFY(writeFile(directory.filePath("mode"), "hangActive"));
        monitor.refresh();
        QTRY_VERIFY(QFile::exists(directory.filePath("pid")));
        const auto pid = readFile(directory.filePath("pid")).toLongLong();
        monitor.setEnabled(false);
        QVERIFY(!monitor.busy());
        QVERIFY(!monitor.m_timer.isActive());
        QVERIFY(!monitor.m_timeout.isActive());
        QTRY_COMPARE(monitor.m_process.state(), QProcess::NotRunning);
        QVERIFY(::kill(pid, 0) == -1 && errno == ESRCH);
        QCOMPARE(monitor.snapshot(), previous);
        QCOMPARE(monitor.updatedAt(), updated);
        QCOMPARE(snapshots.count(), 1);
        const auto calls = readFile(directory.filePath("calls"));
        monitor.refresh();
        QMetaObject::invokeMethod(&monitor.m_timer, "timeout");
        QTest::qWait(50);
        QCOMPARE(readFile(directory.filePath("calls")), calls);

        // Resume into a blocked request, then off/on before its finished signal is delivered.
        QFile::remove(directory.filePath("pid"));
        QVERIFY(writeFile(directory.filePath("mode"), "hang"));
        monitor.setEnabled(true);
        QTRY_VERIFY(QFile::exists(directory.filePath("pid")));
        monitor.setEnabled(false);
        QVERIFY(writeFile(directory.filePath("mode"), "change"));
        monitor.setEnabled(true);
        QTRY_COMPARE(snapshots.count(), 2);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.error().isEmpty());
        QVERIFY(monitor.m_timer.isActive());

        // Exercise the actual timeout callback without waiting fifteen seconds.
        QFile::remove(directory.filePath("pid"));
        QVERIFY(writeFile(directory.filePath("mode"), "hang"));
        monitor.refresh();
        QTRY_VERIFY(QFile::exists(directory.filePath("pid")));
        QMetaObject::invokeMethod(&monitor.m_timeout, "timeout");
        QTRY_COMPARE(monitor.m_process.state(), QProcess::NotRunning);
        QVERIFY(!monitor.busy());
        QVERIFY(!monitor.error().isEmpty());
        QCOMPARE(snapshots.count(), 2);
        QVERIFY(writeFile(directory.filePath("mode"), "error"));
        monitor.refresh();
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.m_timer.interval() >= 60000); // Backoff after consecutive failures.
        QCOMPARE(snapshots.count(), 2);
        QVERIFY(writeFile(directory.filePath("mode"), "change"));
        monitor.refresh();
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.error().isEmpty());
        QCOMPARE(snapshots.count(), 2); // Recovery alone must not rebuild identical rows.
        monitor.setEnabled(false);
    }

    void largeHistoryWatermarkTiesAndCursorFailure()
    {
        QTemporaryDir directory;
        const auto executable = directory.filePath("opencode");
        QVERIFY(writeFile(executable, R"PY(#!/usr/bin/env python3
import json, sys
from pathlib import Path
from urllib.parse import urlparse, parse_qs
base = Path(__file__).parent
mode = (base / 'mode').read_text() if (base / 'mode').exists() else ''
path = sys.argv[3]
with (base / 'calls').open('a') as log: log.write(path + '\n')
query = parse_qs(urlparse(path).query)
if path.endswith('/active'):
    result = {'data': {}}
elif mode == 'badcursor':
    result = {'data': [{'id': 'repeated', 'time': {'updated': 999999}}], 'cursor': {'next': 'same'}}
else:
    rows = [{'id': f's{i:05}', 'time': {'updated': 100000 - i}} for i in range(2500)]
    if mode == 'ties':
        rows = [{'id': f'new{i:05}', 'time': {'updated': 100000}} for i in range(205)] + rows
    start = int(query.get('cursor', ['0'])[0])
    limit = int(query['limit'][0])
    page = rows[start:start + limit]
    result = {'data': page, 'cursor': {'next': str(start + len(page))} if page else {}}
print(json.dumps(result))
)PY"));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        Monitor monitor;
        monitor.setProperty("executable", executable);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotChanged);
        monitor.setEnabled(true);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.error().isEmpty());
        QCOMPARE(monitor.m_cache.size(), 2500);
        QCOMPARE(readFile(directory.filePath("calls")).count('\n'), 5);
        QVERIFY(writeFile(directory.filePath("calls"), {}));
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(readFile(directory.filePath("calls")).count('\n'), 2);
        QVERIFY(writeFile(directory.filePath("mode"), "ties"));
        QVERIFY(writeFile(directory.filePath("calls"), {}));
        monitor.poll(false);
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.error().isEmpty());
        QCOMPARE(monitor.m_cache.size(), 2705);
        QCOMPARE(readFile(directory.filePath("calls")).count('\n'), 4);
        QCOMPARE(snapshots.count(), 2);
        const auto previous = monitor.snapshot();
        QVERIFY(writeFile(directory.filePath("mode"), "badcursor"));
        monitor.refresh();
        QTRY_VERIFY(!monitor.busy());
        QVERIFY(monitor.error().contains("repeated"));
        QCOMPARE(monitor.snapshot(), previous);
        QCOMPARE(snapshots.count(), 2);
    }

    void liveServer()
    {
        if (!qEnvironmentVariableIsSet("OPENCODE_LIVE_TEST"))
            QSKIP("Set OPENCODE_LIVE_TEST=1 to check the real local V2 service.");
        Monitor monitor;
        monitor.setInterval(3600);
        monitor.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(!monitor.busy(), 60000);
        QVERIFY2(monitor.error().isEmpty(), qPrintable(monitor.error()));
        QVERIFY(monitor.updatedAt().isValid());
        qInfo() << "Live snapshot:" << monitor.snapshot().value("sessions").toList().size()
                << "sessions," << monitor.snapshot().value("active").toMap().size() << "active";
    }
};

QTEST_GUILESS_MAIN(MonitorTest)
#include "monitor-test.moc"
