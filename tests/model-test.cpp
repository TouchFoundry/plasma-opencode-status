#include "sessionmodel.h"
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

static QVariantMap session(const QString &id, const QString &parent = {}, const QString &outcome = {}, qint64 updated = 1)
{
    return {{"id", id}, {"parentID", parent}, {"outcome", outcome}, {"time", QVariantMap{{"updated", updated}}}};
}

class ModelTest : public QObject
{
    Q_OBJECT
private slots:
    void semanticsAndStability()
    {
        SessionModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList sessions{session("root"), session("child", "root", "failed"), session("grandchild", "child"),
                              session("stopped", {}, "interrupted"), session("done", "missing", "succeeded")};
        QVariantMap snapshot{{"sessions", sessions}, {"active", QVariantMap{{"grandchild", QVariantMap{{"type", "running"}}}}}};
        model.setSnapshot(snapshot);
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.inactiveCount(), 4);
        auto role = [&model](const QString &id, int role) { return model.data(model.index(model.indexForId(id)), role); };
        QCOMPARE(role("root", SessionModel::Section).toString(), "active");
        QCOMPARE(role("child", SessionModel::Status).toString(), "failed");
        QCOMPARE(role("grandchild", SessionModel::Depth).toInt(), 2);
        QCOMPARE(role("done", SessionModel::Status).toString(), "completed");
        QCOMPARE(role("stopped", SessionModel::Status).toString(), "interrupted");
        QPersistentModelIndex root(model.index(model.indexForId("root")));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
        model.setSnapshot(snapshot);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(layouts.count(), 0);
        model.toggle("root");
        QCOMPARE(root.data(SessionModel::RowId).toString(), "root");
        QCOMPARE(model.indexForId("child"), -1);
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.inactiveCount(), 4);
        model.toggle("root");
        QCOMPARE(role("grandchild", SessionModel::Depth).toInt(), 2);
        // A section change/reorder must keep persistent identity and never reset the model.
        snapshot["active"] = QVariantMap{};
        model.setSnapshot(snapshot);
        QCOMPARE(root.data(SessionModel::RowId).toString(), "root");
        QCOMPARE(root.row(), model.indexForId("root"));
        QCOMPARE(role("root", SessionModel::Section).toString(), "inactive");
        QCOMPARE(resets.count(), 0);
        // One title edit changes one row, without replacing any rows or layout.
        changes.clear();
        layouts.clear();
        auto edited = sessions[0].toMap();
        edited["title"] = "Renamed";
        sessions[0] = edited;
        snapshot["sessions"] = sessions;
        model.setSnapshot(snapshot);
        QCOMPARE(changes.count(), 1);
        QCOMPARE(layouts.count(), 0);
    }

    void archiveCyclesAndDeletion()
    {
        SessionModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto archived = session("archived");
        archived["time"] = QVariantMap{{"updated", 1}, {"archived", 1}};
        QVariantList sessions{session("a", "b"), session("b", "a"), session("c", "c"), archived};
        model.setSnapshot({{"sessions", sessions}, {"active", QVariantMap{}}});
        QCOMPARE(model.inactiveCount(), 3);
        QCOMPARE(model.indexForId("archived"), -1);
        model.setSnapshot({{"sessions", sessions}, {"active", QVariantMap{{"archived", QVariantMap{}}}}});
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.inactiveCount(), 3);
        QVERIFY(model.indexForId("archived") >= 0);
        model.setSnapshot({{"sessions", QVariantList{}}, {"active", QVariantMap{}}});
        QCOMPARE(model.activeCount(), 0);
        QCOMPARE(model.inactiveCount(), 0);
        QCOMPARE(model.rowCount(), 4); // two headers and two empty labels
    }

    void largeHistory()
    {
        SessionModel model;
        QVariantList sessions;
        const int count = 10000;
        for (int i = 0; i < count; ++i) sessions.append(session(QString::number(i), {}, {}, i));
        QVariantMap snapshot{{"sessions", sessions}, {"active", QVariantMap{}}};
        QElapsedTimer timer;
        timer.start();
        model.setSnapshot(snapshot);
        const auto initial = timer.nsecsElapsed();
        QCOMPARE(model.inactiveCount(), count);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        timer.restart();
        for (int i = 0; i < 100; ++i) model.setSnapshot(snapshot);
        const auto unchanged = timer.nsecsElapsed();
        QCOMPARE(changes.count(), 0);
        QCOMPARE(resets.count(), 0);
        auto edited = sessions[0].toMap();
        edited["title"] = "Changed";
        sessions[0] = edited;
        snapshot["sessions"] = sessions;
        timer.restart();
        model.setSnapshot(snapshot);
        qInfo() << "10,000 sessions: initial ms" << initial / 1e6
                << "; 100 identical snapshots ms" << unchanged / 1e6
                << "; single title edit ms" << timer.nsecsElapsed() / 1e6;
        QCOMPARE(changes.count(), 1);
        // Deep trees must not overflow the C++/JS stack or perform quadratic ancestor walks.
        for (int i = 1; i < count; ++i) sessions[i] = session(QString::number(i), QString::number(i - 1));
        model.setSnapshot({{"sessions", sessions}, {"active", QVariantMap{{QString::number(count - 1), QVariantMap{}}}}});
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.inactiveCount(), count - 1);
        QCOMPARE(model.data(model.index(model.indexForId(QString::number(count - 1))), SessionModel::Depth).toInt(), count - 1);
        model.toggle("0");
        QCOMPARE(model.indexForId("1"), -1);
    }
};

QTEST_GUILESS_MAIN(ModelTest)
#include "model-test.moc"
