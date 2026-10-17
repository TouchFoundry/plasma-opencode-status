import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    property alias cfg_executable: executable.text
    property alias cfg_interval: interval.value
    TextField {
        id: executable
        Kirigami.FormData.label: i18n("OpenCode executable:")
        placeholderText: "/home/you/.opencode/bin/opencode"
    }
    SpinBox {
        id: interval
        Kirigami.FormData.label: i18n("Refresh interval (seconds):")
        from: 10
        to: 3600
    }
    Label {
        text: i18n("Uses the official CLI for server discovery and authentication. No credentials are stored by this widget.")
        wrapMode: Text.Wrap
    }
}
