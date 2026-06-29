import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../theme"

Button {
    id: control
    Layout.fillWidth: true
    Layout.preferredHeight: 44
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 14

    property bool active: false

    background: Rectangle {
        radius: QbxTheme.radiusSmall
        color: {
            if (control.active)
                return Qt.rgba(QbxTheme.accent.r, QbxTheme.accent.g, QbxTheme.accent.b, 0.18)
            if (control.hovered)
                return QbxTheme.bgHover
            return "transparent"
        }
        border.width: control.active ? 1 : 0
        border.color: control.active ? QbxTheme.accent : "transparent"
    }

    contentItem: Text {
        text: control.text
        color: control.active ? QbxTheme.accentGlow : QbxTheme.textSecondary
        font.pixelSize: control.font.pixelSize
        font.weight: control.active ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        leftPadding: 4
    }
}
