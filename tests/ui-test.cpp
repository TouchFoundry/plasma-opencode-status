#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QtTest>

class UiTest : public QObject
{
    Q_OBJECT
private slots:
    void componentsCompile()
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QML_BUILD_DIR));
        for (const auto &path : {"ui/main.qml", "ui/AgentList.qml", "ui/configGeneral.qml", "config/config.qml"}) {
            QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/package/contents/") + path));
            QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 10000);
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        }
    }

    void virtualizedListAndFocus()
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QML_BUILD_DIR));
        // Plasma normally injects i18n; the smoke test only needs untranslated strings.
        engine.globalObject().setProperty("i18n", engine.evaluate("(function(text) { return text; })"));
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import org.touchfoundry.opencodestatus
import "../package/contents/ui" as Widget
Item {
    width: 500
    height: 400
    SessionModel { id: sessionData; objectName: "sessionModel" }
    Widget.AgentList {
        objectName: "list"
        anchors.fill: parent
        sessionModel: sessionData
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/tests/section.qml")));
        QTRY_VERIFY(!component.isLoading());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(item);
        QQuickWindow window;
        window.resize(500, 400);
        item->setParentItem(window.contentItem());
        auto *model = object->findChild<QAbstractItemModel *>("sessionModel");
        auto *list = object->findChild<QQuickItem *>("list");
        QVERIFY(model);
        QVERIFY(list);
        QVariantList sessions;
        for (int i = 0; i < 10000; ++i)
            sessions.append(QVariantMap{{"id", QString::number(i)}, {"title", "Session"}, {"time", QVariantMap{{"updated", i}}}});
        sessions.append(QVariantMap{{"id", "root"}, {"title", "Parent"}, {"time", QVariantMap{{"updated", 20000}}}});
        sessions.append(QVariantMap{{"id", "child"}, {"parentID", "root"}, {"outcome", "failed"}});
        QVariantMap snapshot{{"sessions", sessions}, {"active", QVariantMap{}}};
        QElapsedTimer timer;
        timer.start();
        model->setProperty("snapshot", snapshot);
        window.show();
        window.requestActivate();
        QTRY_VERIFY(list->property("count").toInt() > 10000);
        const auto countRows = [](QQuickItem *root, auto &&self) -> int {
            int count = root->objectName() == "agentRow";
            for (auto *child : root->childItems()) count += self(child, self);
            return count;
        };
        QTRY_VERIFY(countRows(list, countRows) > 0);
        QVERIFY2(countRows(list, countRows) < 30, "Offscreen rows must not be instantiated");
        qInfo() << "10,002 sessions: visible/pool delegates" << countRows(list, countRows)
                << "; model + initial offscreen layout ms" << timer.nsecsElapsed() / 1e6;
        const auto indexFor = [model](const QString &id) {
            int index = -1;
            QMetaObject::invokeMethod(model, "indexForId", Q_RETURN_ARG(int, index), Q_ARG(QString, id));
            return index;
        };
        list->setProperty("currentIndex", indexFor("root"));
        list->forceActiveFocus();
        QTRY_VERIFY(list->hasActiveFocus());
        auto rootSession = sessions[10000].toMap();
        rootSession["title"] = "Renamed";
        sessions[10000] = rootSession;
        snapshot["sessions"] = sessions;
        model->setProperty("snapshot", snapshot);
        QVERIFY(list->hasActiveFocus());
        QCOMPARE(list->property("currentIndex").toInt(), indexFor("root"));
        QTest::keyClick(&window, Qt::Key_Space);
        QTRY_COMPARE(indexFor("child"), -1);
        QVERIFY(list->hasActiveFocus());
        QCOMPARE(list->property("currentIndex").toInt(), indexFor("root"));
        snapshot["active"] = QVariantMap{{"root", QVariantMap{{"type", "running"}}}};
        model->setProperty("snapshot", snapshot);
        QTRY_COMPARE(list->property("currentIndex").toInt(), indexFor("root"));
        QVERIFY(list->hasActiveFocus());
        QVERIFY(countRows(list, countRows) < 30);
        // Scrolling to the far end should reuse a bounded number of delegates too.
        list->setProperty("currentIndex", model->rowCount() - 1);
        QMetaObject::invokeMethod(list, "positionViewAtEnd");
        QTest::qWait(50);
        QVERIFY(countRows(list, countRows) < 30);
        item->setParentItem(nullptr);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ui-test.moc"
