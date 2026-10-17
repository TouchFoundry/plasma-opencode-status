#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QQmlExpression>
#include <QtTest>
#ifdef HAVE_PLASMA_INTEGRATION_TEST
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <Plasma/PluginLoader>
#include <PlasmaQuick/AppletQuickItem>
#include <plasmaquick/plasmaquick.h>
#include <KPackage/PackageLoader>
#endif

class UiTest : public QObject
{
    Q_OBJECT
private slots:
#ifdef HAVE_PLASMA_INTEGRATION_TEST
    void realAppletPopup()
    {
        auto engine = PlasmaQuick::globalEngine();
        engine->addImportPath(QStringLiteral(QML_BUILD_DIR));
        class TestCorona : public Plasma::Corona {
        public:
            QRect screenGeometry(uint) const override { return QRect(0, 0, 800, 600); }
        } corona;
        auto shell = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/Shell"));
        shell.setPath(QStringLiteral("org.kde.plasma.desktop"));
        corona.setKPackage(shell);
        auto *containment = corona.createContainment(QStringLiteral("org.kde.plasma.panel"));
        QVERIFY(containment);
        containment->setFormFactor(Plasma::Types::Horizontal);
        auto applet = Plasma::PluginLoader::self()->loadApplet(QStringLiteral(SOURCE_DIR "/package"));
        QVERIFY(applet);
        auto *rawApplet = applet.get();
        containment->addApplet(std::move(applet));
        auto *item = PlasmaQuick::AppletQuickItem::itemForApplet(rawApplet);
        QVERIFY(item);
        QVERIFY2(!rawApplet->failedToLaunch(), qPrintable(rawApplet->launchErrorMessage()));
        // SystemTrayState.setActiveApplet only selects applets with no preference.
        QVERIFY2(!item->preferredRepresentation(), "An explicit representation prevents the tray popup from activating");
        item->setSize(QSizeF(22, 22));
        QVERIFY(item->compactRepresentationItem());
        QQmlExpression clicked(qmlContext(item->compactRepresentationItem()), item->compactRepresentationItem(), "clicked(null)");
        clicked.evaluate();
        QVERIFY2(!clicked.hasError(), qPrintable(clicked.error().toString()));
        QVERIFY(item->isExpanded());
        QVERIFY(item->fullRepresentationItem());
        clicked.evaluate();
        QVERIFY(!item->isExpanded());
    }
#endif
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

    void compactActivationAndSmallTrayLayout()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import "../package/contents/ui" as Widget
Widget.CompactIndicator {
    width: 22
    height: 22
    available: false
    activeCount: 3
    inactiveCount: 7
    accessibleText: "Sync paused"
    property bool expanded: false
    onActivated: expanded = !expanded
}
)QML", QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/tests/compact.qml")));
        QTRY_VERIFY(!component.isLoading());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(item);
        // Match the tray's MouseArea discovery heuristic, not just a button signal.
        QVERIFY(object->inherits("QQuickMouseArea"));
        QQuickWindow window;
        window.resize(22, 22);
        item->setParentItem(window.contentItem());
        window.show();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(11, 11));
        QTRY_VERIFY(object->property("expanded").toBool());
        // The tray can also forward clicked(mouse) instead of dispatching input.
        QQmlExpression forwarded(qmlContext(object.data()), object.data(), "clicked(null)");
        forwarded.evaluate();
        QVERIFY2(!forwarded.hasError(), qPrintable(forwarded.error().toString()));
        QVERIFY(!object->property("expanded").toBool());
        item->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space);
        QTRY_VERIFY(object->property("expanded").toBool());
        auto *active = object->findChild<QQuickItem *>("activeCountLabel");
        auto *inactive = object->findChild<QQuickItem *>("inactiveCountLabel");
        QVERIFY(active);
        QVERIFY(inactive);
        QCOMPARE(active->height(), 11);
        QCOMPARE(inactive->height(), 11);
        QCOMPARE(inactive->y(), 11);
        QCOMPARE(inactive->y() + inactive->height(), item->height());
        QCOMPARE(active->property("text").toString(), QStringLiteral("—"));
        object->setProperty("available", true);
        QCOMPARE(active->property("text").toString(), QStringLiteral("3"));
        QCOMPARE(inactive->property("text").toString(), QStringLiteral("7"));
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
