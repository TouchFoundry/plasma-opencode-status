pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PC

ListView {
    id: view
    required property var sessionModel
    property string savedId: ""
    property int savedIndex: -1
    model: sessionModel
    clip: true
    reuseItems: true
    cacheBuffer: 0
    activeFocusOnTab: true
    keyNavigationEnabled: false
    boundsBehavior: Flickable.StopAtBounds
    PC.ScrollBar.vertical: PC.ScrollBar {}

    function step(direction) {
        let next = currentIndex + direction;
        while (next >= 0 && next < count) {
            if (sessionModel.idAt(next).charAt(0) !== "@") {
                currentIndex = next;
                positionViewAtIndex(next, ListView.Contain);
                return;
            }
            next += direction;
        }
    }
    Keys.onDownPressed: step(1)
    Keys.onUpPressed: step(-1)
    Keys.onSpacePressed: sessionModel.toggle(sessionModel.idAt(currentIndex))
    Keys.onReturnPressed: sessionModel.toggle(sessionModel.idAt(currentIndex))
    Connections {
        target: view.sessionModel
        function onUpdating() {
            view.savedIndex = view.currentIndex;
            view.savedId = view.sessionModel.idAt(view.currentIndex);
        }
        function onUpdated() {
            const index = view.sessionModel.indexForId(view.savedId);
            view.currentIndex = index >= 0 ? index : Math.min(view.savedIndex, view.count - 1);
        }
    }

    delegate: PC.ItemDelegate {
        id: row
        required property int index
        required property string rowId
        required property string title
        required property string agent
        required property string directory
        required property string status
        required property string section
        required property string kind
        required property int depth
        required property bool hasChildren
        required property bool collapsed
        objectName: "agentRow"
        width: view.width
        // Fixed heights keep ListView's extent estimate stable while delegates are recycled.
        height: kind === "session" ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 2
        leftPadding: Kirigami.Units.smallSpacing + (kind === "session" ? Math.min(depth, 12) * Kirigami.Units.gridUnit : 0)
        focusPolicy: Qt.NoFocus // Keyboard focus belongs to the stable ListView, not recycled rows.
        highlighted: view.activeFocus && view.currentIndex === index && kind === "session"
        enabled: kind === "session"
        Accessible.name: kind === "session" ? title + ", " + statusLabel.text : heading.text
        onClicked: {
            view.currentIndex = index;
            view.forceActiveFocus();
            if (hasChildren) view.sessionModel.toggle(rowId);
        }
        contentItem: Item {
            PC.Label {
                id: heading
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                visible: row.kind !== "session"
                font.bold: row.kind === "header"
                text: row.kind === "header"
                    ? (row.section === "active" ? i18n("Active · %1", view.sessionModel.activeCount) : i18n("Inactive · %1", view.sessionModel.inactiveCount))
                    : (row.section === "active" ? i18n("No agents running") : i18n("No inactive agent trees"))
            }
            RowLayout {
                anchors.fill: parent
                visible: row.kind === "session"
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    source: row.hasChildren ? (row.collapsed ? "arrow-right" : "arrow-down") : "go-next"
                    opacity: row.hasChildren ? 1 : 0.3
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    PC.Label {
                        Layout.fillWidth: true
                        text: row.title
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                    PC.Label {
                        Layout.fillWidth: true
                        text: [row.agent, row.directory].filter(Boolean).join(" · ")
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                    }
                }
                PC.Label {
                    id: statusLabel
                    text: {
                        switch (row.status) {
                        case "running": return i18n("Running");
                        case "failed": return i18n("Error");
                        case "interrupted": return i18n("Stopped");
                        case "completed": return i18n("Completed");
                        default: return i18n("Idle");
                        }
                    }
                    color: row.status === "failed" ? Kirigami.Theme.negativeTextColor
                        : row.status === "running" ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.textColor
                }
            }
        }
        PC.ToolTip.text: title + "\n" + directory + "\n" + rowId
        PC.ToolTip.visible: hovered && kind === "session"
    }
}
