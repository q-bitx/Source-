import QtQuick
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root

    property alias title: titleText.text
    property bool compact: false
    default property alias children: innerContent.data

    color: QbxTheme.bgCard
    radius: QbxTheme.radiusMedium
    border.color: QbxTheme.border
    border.width: 1
    implicitHeight: Math.max(compact ? 0 : QbxTheme.cardMinHeight, contentColumn.implicitHeight + 28)

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Text {
            id: titleText
            visible: text !== ""
            font.pixelSize: 15
            font.weight: Font.DemiBold
            color: QbxTheme.accentGlow
            Layout.fillWidth: true
        }

        ColumnLayout {
            id: innerContent
            Layout.fillWidth: true
            spacing: 6
        }
    }
}
