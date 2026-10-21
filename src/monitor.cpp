#include "monitor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <algorithm>

// Only retain fields used by the widget. Token/usage/view changes must not rebuild the UI.
static QVariantMap displaySession(const QJsonObject &session)
{
    const auto time = session.value("time").toObject();
    return {{"id", session.value("id").toString()},
            {"parentID", session.value("parentID").toString()},
            {"title", session.value("title").toString()},
            {"agent", session.value("agent").toString()},
            {"outcome", session.value("outcome").toString()},
            {"location", QVariantMap{{"directory", session.value("location").toObject().value("directory").toString()}}},
            {"time", QVariantMap{{"updated", time.value("updated").toVariant()},
                                 {"archived", time.value("archived").toVariant()}}}};
}

Monitor::Monitor(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(30000);
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { poll(false); });
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(15000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_cancelled = true;
        m_process.kill();
        fail(tr("OpenCode did not respond within 15 seconds."));
    });
    connect(&m_process, &QProcess::finished, this, &Monitor::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Cannot start OpenCode. Check the executable path in widget settings."));
    });
}

Monitor::~Monitor()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void Monitor::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_timer.stop();
    if (!enabled) {
        m_timeout.stop();
        m_busy = false;
        m_pending.clear();
        m_missing.clear();
        m_ids.clear();
        m_cursors.clear();
        m_cancelled = m_process.state() != QProcess::NotRunning;
        if (m_cancelled)
            m_process.kill();
    }
    emit enabledChanged();
    emit stateChanged();
    if (enabled)
        poll(true); // Resume reconciles deletions/archives and any changes while paused.
}

void Monitor::setInterval(int seconds)
{
    seconds = std::clamp(seconds, 10, 3600);
    if (m_interval == seconds)
        return;
    m_interval = seconds;
    if (m_timer.isActive()) schedule();
    emit intervalChanged();
}

void Monitor::refresh()
{
    poll(true);
}

void Monitor::setMaxDays(int days)
{
    days = std::clamp(days, 0, 3650);
    if (m_maxDays == days)
        return;
    m_maxDays = days;
    emit maxDaysChanged();
    m_reconciled.invalidate();
    if (m_enabled) {
        // Reuse pause/resume cancellation so no partial poll with the old window
        // can publish over the new one. Paused widgets must not start any work.
        setEnabled(false);
        setEnabled(true);
    }
}

void Monitor::poll(bool full)
{
    if (!m_enabled || m_busy || m_process.state() != QProcess::NotRunning)
        return;
    m_timer.stop();
    m_busy = true;
    m_cancelled = false;
    m_full = full || !m_reconciled.isValid() || m_reconciled.elapsed() >= 600000;
    m_cutoff = m_maxDays ? QDateTime::currentMSecsSinceEpoch() - qint64(m_maxDays) * 86400000 : 0;
    m_pending.clear();
    m_pendingWatermark = m_full ? 0 : m_watermark;
    m_ids.clear();
    m_cursors.clear();
    m_missing.clear();
    emit stateChanged();
    request(m_full ? QStringLiteral("/api/session?limit=1000") : QStringLiteral("/api/session?limit=100"), Request::Sessions);
}

void Monitor::request(const QString &path, Request kind)
{
    m_request = kind;
    // The official CLI owns discovery and authentication. No shell or detached child.
    m_process.setProgram(m_executable);
    m_process.setArguments({QStringLiteral("api"), QStringLiteral("get"), path});
    m_process.start();
    m_timeout.start();
}

void Monitor::finished(int code, QProcess::ExitStatus status)
{
    m_timeout.stop();
    if (m_cancelled) {
        m_cancelled = false;
        // A quick off/on must wait for the cancelled process to be reaped.
        if (m_enabled && !m_timer.isActive())
            poll(true);
        return;
    }
    if (!m_busy || !m_enabled)
        return;
    if (code != 0 || status != QProcess::NormalExit) {
        fail(tr("Cannot query OpenCode. Check that its V2 service is available."));
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(m_process.readAllStandardOutput(), &error);
    m_process.readAllStandardError();
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(tr("OpenCode returned invalid JSON."));
        return;
    }
    const auto object = document.object();
    const auto data = object.value("data");
    if (m_request == Request::Sessions) {
        if (!data.isArray() || !object.value("cursor").isObject()) {
            fail(tr("Unsupported session response. OpenCode V2 is required."));
            return;
        }
        bool crossedWatermark = false;
        bool crossedCutoff = false;
        for (const auto &value : data.toArray()) {
            const auto session = displaySession(value.toObject());
            const auto id = session.value("id").toString();
            if (id.isEmpty()) {
                fail(tr("OpenCode returned a session without an ID."));
                return;
            }
            const auto updated = session.value("time").toMap().value("updated").toLongLong();
            crossedWatermark |= updated < m_watermark;
            m_pendingWatermark = std::max(m_pendingWatermark, updated);
            if (m_cutoff && updated < m_cutoff) {
                crossedCutoff = true;
                continue;
            }
            if (!m_ids.contains(id)) {
                m_ids.insert(id);
                m_pending.insert(id, session);
            }
        }
        const auto cursor = object.value("cursor").toObject().value("next").toString();
        // V2 sorts by updated time descending. Include the entire watermark tie group.
        // Reconciliation catches deletions and edits that do not bump updated time.
        if (!crossedCutoff && !data.toArray().isEmpty() && !cursor.isEmpty() && (m_full || (!crossedWatermark && data.toArray().size() >= 100))) {
            if (m_cursors.contains(cursor)) {
                fail(tr("OpenCode returned a repeated pagination cursor."));
                return;
            }
            m_cursors.insert(cursor);
            request(QStringLiteral("/api/session?limit=%1&cursor=").arg(m_full ? 1000 : 100)
                        + QString::fromLatin1(QUrl::toPercentEncoding(cursor)), Request::Sessions);
            return;
        }
        request(QStringLiteral("/api/session/active"), Request::Active);
        return;
    }
    if (!data.isObject()) {
        fail(tr("Unsupported OpenCode response. OpenCode V2 is required."));
        return;
    }
    if (m_request == Request::Active) {
        m_active = data.toObject().toVariantMap();
        // Refresh running and newly stopped sessions, even if pagination raced their transition.
        const auto previous = m_snapshot.value("active").toMap();
        QSet<QString> hydrate;
        for (auto it = m_active.cbegin(); it != m_active.cend(); ++it)
            if (!m_ids.contains(it.key())) hydrate.insert(it.key());
        for (auto it = previous.cbegin(); it != previous.cend(); ++it)
            if (!m_active.contains(it.key()) && !m_ids.contains(it.key()) && !m_full) hydrate.insert(it.key());
        m_missing = hydrate.values();
        fetchMissing();
        return;
    }
    const auto session = displaySession(data.toObject());
    const auto id = session.value("id").toString();
    if (id.isEmpty()) {
        fail(tr("OpenCode returned a session without an ID."));
        return;
    }
    m_pending.insert(id, session);
    fetchMissing();
}

void Monitor::fetchMissing()
{
    if (m_missing.isEmpty()) {
        complete();
        return;
    }
    request(QStringLiteral("/api/session/") + QString::fromLatin1(QUrl::toPercentEncoding(m_missing.takeFirst())), Request::Missing);
}

void Monitor::complete()
{
    bool changed = m_snapshot.isEmpty() || m_active != m_snapshot.value("active").toMap();
    // Excluded archive history need not occupy the display cache. Keep its watermark,
    // and hydrate it again if the live map later reports it running.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it.value().value("time").toMap().value("archived").toLongLong() && !m_active.contains(it.key())) {
            if (!m_full) changed |= m_cache.remove(it.key()) > 0;
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    if (m_full) {
        if (m_cache != m_pending) {
            changed = true;
            m_cache = std::move(m_pending);
        }
        // Keep shared display maps on identical scans; release any old cache after a swap.
        m_pending.clear();
        m_reconciled.restart();
    } else {
        for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
            if (m_cache.value(it.key()) != it.value()) {
                m_cache.insert(it.key(), it.value());
                changed = true;
            }
        }
        m_pending.clear();
    }
    m_watermark = m_pendingWatermark;
    // Sessions can age out even when the server returns no changed display fields.
    // Keep running agents regardless of age; a newly stopped old agent expires here.
    if (m_cutoff) {
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (!m_active.contains(it.key()) && it.value().value("time").toMap().value("updated").toLongLong() < m_cutoff) {
                it = m_cache.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    m_ids.clear();
    m_cursors.clear();
    m_updatedAt = QDateTime::currentDateTime();
    if (changed) {
        QVariantList sessions;
        sessions.reserve(m_cache.size());
        for (const auto &session : std::as_const(m_cache)) sessions.append(session);
        m_snapshot = {{"sessions", sessions}, {"active", m_active}};
        emit snapshotChanged();
    }
    m_error.clear();
    m_failures = 0;
    m_busy = false;
    schedule();
    emit stateChanged();
}

void Monitor::schedule()
{
    if (m_enabled) {
        const int delay = std::max(interval(), m_failures ? std::min(300, 30 * (1 << std::min(m_failures - 1, 4))) : 0);
        m_timer.start(delay * 1000);
    }
}

void Monitor::fail(const QString &message)
{
    m_timeout.stop();
    m_error = message;
    m_busy = false;
    m_pending.clear();
    m_ids.clear();
    m_cursors.clear();
    m_missing.clear();
    ++m_failures;
    schedule();
    emit stateChanged();
}
