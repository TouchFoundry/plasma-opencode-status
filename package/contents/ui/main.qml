pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PC
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.touchfoundry.opencodestatus

PlasmoidItem {
    id: root
    readonly property bool available: monitor.enabled && monitor.hasSnapshot && !monitor.error

    Plasmoid.icon: "view-process-tree"
    Plasmoid.status: monitor.enabled && monitor.error ? PlasmaCore.Types.NeedsAttentionStatus : PlasmaCore.Types.ActiveStatus
    preferredRepresentation: compactRepresentation
    toolTipMainText: i18n("OpenCode Status")
    toolTipSubText: !monitor.enabled ? i18n("Sync paused — counts are not live") : monitor.error || (available
        ? i18n("%1 active · %2 inactive (including subagents)", tree.activeCount, tree.inactiveCount)
        : i18n("Connecting to OpenCode…"))

    Monitor {
        id: monitor
        executable: Plasmoid.configuration.executable
        interval: Plasmoid.configuration.interval
        Component.onCompleted: enabled = Plasmoid.configuration.syncEnabled
    }
    Connections {
        target: Plasmoid.configuration
        function onSyncEnabledChanged() { monitor.enabled = Plasmoid.configuration.syncEnabled; }
    }
    SessionModel { id: tree; snapshot: monitor.snapshot }

    compactRepresentation: PC.ToolButton {
        implicitWidth: Kirigami.Units.iconSizes.medium
        implicitHeight: Kirigami.Units.iconSizes.medium
        text: i18n("OpenCode Status")
        Accessible.name: root.toolTipSubText
        onClicked: root.expanded = !root.expanded
        contentItem: Column {
            spacing: 0
            PC.Label {
                width: parent.width
                height: parent.height / 2
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Math.max(9, Math.min(14, parent.height / 2))
                font.bold: true
                color: Kirigami.Theme.positiveTextColor
                text: root.available ? tree.activeCount : "—"
            }
            PC.Label {
                width: parent.width
                height: parent.height / 2
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Math.max(9, Math.min(14, parent.height / 2))
                text: root.available ? tree.inactiveCount : "—"
            }
        }
    }

    fullRepresentation: ColumnLayout {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 19
        Layout.minimumHeight: Kirigami.Units.gridUnit * 15
        Layout.preferredWidth: Kirigami.Units.gridUnit * 28
        Layout.preferredHeight: Kirigami.Units.gridUnit * 30
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Kirigami.Heading { text: i18n("OpenCode"); Layout.fillWidth: true }
            PC.Switch {
                text: i18n("Sync")
                checked: Plasmoid.configuration.syncEnabled
                onToggled: Plasmoid.configuration.syncEnabled = checked
                PC.ToolTip.text: i18n("Enable background updates. Turning this off cancels the current sync.")
                PC.ToolTip.visible: hovered
            }
            PC.ToolButton {
                icon.name: "view-refresh"
                text: i18n("Refresh")
                display: PC.AbstractButton.IconOnly
                enabled: monitor.enabled && !monitor.busy
                onClicked: monitor.refresh()
                PC.ToolTip.text: text
                PC.ToolTip.visible: hovered
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !monitor.enabled || !!monitor.error
            type: monitor.enabled ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
            text: !monitor.enabled
                ? i18n("Sync paused. No background requests. Any displayed sessions are from the last sync.")
                : monitor.error + (monitor.hasSnapshot ? " " + i18n("Showing the last successful snapshot.") : "")
        }
        PC.Label {
            Layout.fillWidth: true
            text: monitor.hasSnapshot
                ? i18n("Updated %1 · counts include subagents", Qt.formatDateTime(monitor.updatedAt, Qt.locale().dateTimeFormat(Locale.ShortFormat)))
                : monitor.enabled ? i18n("Connecting to the OpenCode V2 service…") : i18n("Enable Sync to load sessions")
            opacity: 0.7
            wrapMode: Text.Wrap
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.expanded
            sourceComponent: AgentList { sessionModel: tree }
        }
    }
}
