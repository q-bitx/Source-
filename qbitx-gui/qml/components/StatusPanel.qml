import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    visible: (message || "").length > 0
    radius: 12
    border.width: 1
    Layout.alignment: Qt.AlignHCenter
    Layout.fillWidth: false
    Layout.maximumWidth: 720
    Layout.preferredWidth: 720

    property string message: ""
    property string panelType: "info"   // "error" | "info" | "warning"

    color: {
        if (panelType === "error") return "#f8d7da"
        if (panelType === "warning") return "#fff3cd"
        return "#e7f3ff"
    }
    border.color: {
        if (panelType === "error") return "#dc3545"
        if (panelType === "warning") return "#ffc107"
        return "#0d6efd"
    }

    implicitHeight: contentText.visible ? contentText.implicitHeight + 28 : 0

    Text {
        id: contentText
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        text: root.message
        wrapMode: Text.WordWrap
        font.pixelSize: 15
        color: {
            if (root.panelType === "error") return "#721c24"
            if (root.panelType === "warning") return "#856404"
            return "#0c5460"
        }
        visible: (text || "").length > 0
    }
}
