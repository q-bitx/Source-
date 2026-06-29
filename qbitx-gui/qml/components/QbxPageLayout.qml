import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: root

    default property alias content: contentColumn.data

    readonly property int contentWidth: Math.max(0, width - QbxTheme.pagePadding * 2)
    readonly property int columnWidth: Math.min(QbxTheme.contentMaxWidth, contentWidth)

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: contentColumn
            width: root.columnWidth
            anchors.left: parent.left
            anchors.leftMargin: QbxTheme.pagePadding
            anchors.top: parent.top
            anchors.topMargin: QbxTheme.pagePadding
            anchors.bottomMargin: QbxTheme.pagePadding
            spacing: 20
        }
    }
}
