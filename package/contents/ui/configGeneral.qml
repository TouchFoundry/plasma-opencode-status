import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    property alias cfg_executable: executable.text
    property alias cfg_interval: interval.value
    property alias cfg_maxDays: maxDays.value
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
    SpinBox {
        id: maxDays
        Kirigami.FormData.label: i18n("Max days to show (0 = all):")
        from: 0
        to: 3650
    }
}
