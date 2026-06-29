import QtQuick
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: root

    property string label: ""
    property string value: "—"
    property bool highlight: false

    spacing: 12
    Layout.fillWidth: true

    Text {
        text: root.label
        font.pixelSize: 13
        color: QbxTheme.textSecondary
        Layout.preferredWidth: 160
        Layout.maximumWidth: 160
        elide: Text.ElideRight
    }

    Text {
        text: root.value
        font.pixelSize: 14
        font.weight: root.highlight ? Font.DemiBold : Font.Normal
        color: root.highlight ? QbxTheme.textPrimary : QbxTheme.textPrimary
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
    }
}
