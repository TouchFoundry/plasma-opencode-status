import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PC

// The system tray discovers a root/full-size MouseArea and forwards clicks to it.
// A Controls ToolButton does not provide that forwarding interface.
MouseArea {
    id: indicator
    required property bool available
    required property int activeCount
    required property int inactiveCount
    required property string accessibleText
    signal activated()

    implicitWidth: Kirigami.Units.iconSizes.medium
    implicitHeight: Kirigami.Units.iconSizes.medium
    acceptedButtons: Qt.LeftButton
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleText
    Accessible.onPressAction: activated()
    onClicked: activated()
    Keys.onSpacePressed: activated()
    Keys.onReturnPressed: activated()
    Keys.onEnterPressed: activated()

    Column {
        anchors.fill: parent
        spacing: 0
        PC.Label {
            objectName: "activeCountLabel"
            width: indicator.width
            height: indicator.height / 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Math.max(1, Math.min(14, height))
            font.bold: true
            color: Kirigami.Theme.positiveTextColor
            text: indicator.available ? indicator.activeCount : "—"
        }
        PC.Label {
            objectName: "inactiveCountLabel"
            width: indicator.width
            height: indicator.height / 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Math.max(1, Math.min(14, height))
            text: indicator.available ? indicator.inactiveCount : "—"
        }
    }
}
