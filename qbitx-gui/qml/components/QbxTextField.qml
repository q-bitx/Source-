import QtQuick
import QtQuick.Controls
import "../theme"

TextField {
    color: QbxTheme.textPrimary
    placeholderTextColor: QbxTheme.textMuted
    selectionColor: QbxTheme.accent
    selectedTextColor: "#042028"
    font.pixelSize: 13
    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12

    background: Rectangle {
        radius: QbxTheme.radiusSmall
        color: QbxTheme.bgInput
        border.color: parent.activeFocus ? QbxTheme.accent : QbxTheme.border
        border.width: 1
    }
}
