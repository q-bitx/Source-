import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../theme"

Rectangle {
    id: root
    visible: (message || "").length > 0
    radius: QbxTheme.radiusSmall
    border.width: 1
    Layout.fillWidth: true

    property string message: ""
    property string panelType: "info"   // "error" | "info" | "warning"

    color: {
        if (panelType === "error") return Qt.rgba(QbxTheme.error.r, QbxTheme.error.g, QbxTheme.error.b, 0.12)
        if (panelType === "warning") return Qt.rgba(QbxTheme.warning.r, QbxTheme.warning.g, QbxTheme.warning.b, 0.12)
        return Qt.rgba(QbxTheme.info.r, QbxTheme.info.g, QbxTheme.info.b, 0.12)
    }
    border.color: {
        if (panelType === "error") return Qt.rgba(QbxTheme.error.r, QbxTheme.error.g, QbxTheme.error.b, 0.45)
        if (panelType === "warning") return Qt.rgba(QbxTheme.warning.r, QbxTheme.warning.g, QbxTheme.warning.b, 0.45)
        return Qt.rgba(QbxTheme.info.r, QbxTheme.info.g, QbxTheme.info.b, 0.45)
    }

    implicitHeight: contentText.visible ? contentText.implicitHeight + 24 : 0

    Text {
        id: contentText
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        text: root.message
        wrapMode: Text.WordWrap
        font.pixelSize: 13
        color: {
            if (root.panelType === "error") return QbxTheme.error
            if (root.panelType === "warning") return QbxTheme.warning
            return QbxTheme.info
        }
        visible: (text || "").length > 0
    }
}
