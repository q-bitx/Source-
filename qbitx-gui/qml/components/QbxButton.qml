import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control

    property bool primary: false
    property bool compact: false

    property bool uniform: false

    implicitHeight: uniform ? QbxTheme.actionButtonHeight : (compact ? 36 : 42)
    implicitWidth: uniform
                   ? QbxTheme.actionButtonMinWidth
                   : Math.max(compact ? 88 : 100, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: compact ? 14 : 18
    rightPadding: compact ? 14 : 18
    font.pixelSize: compact ? 13 : 14
    font.weight: primary ? Font.DemiBold : Font.Normal

    background: Rectangle {
        radius: QbxTheme.radiusSmall
        color: {
            if (!control.enabled)
                return Qt.darker(QbxTheme.bgInput, 1.1)
            if (control.primary)
                return control.pressed ? QbxTheme.accentDim : (control.hovered ? QbxTheme.accentGlow : QbxTheme.accent)
            return control.pressed ? QbxTheme.bgActive : (control.hovered ? QbxTheme.bgHover : QbxTheme.bgInput)
        }
        border.width: control.primary ? 0 : 1
        border.color: control.enabled ? QbxTheme.border : Qt.darker(QbxTheme.border, 1.2)
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: control.font.pixelSize
        font.weight: control.font.weight
        color: {
            if (!control.enabled)
                return QbxTheme.textMuted
            if (control.primary)
                return "#042028"
            return QbxTheme.textPrimary
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
