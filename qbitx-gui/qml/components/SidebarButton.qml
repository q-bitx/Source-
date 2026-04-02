import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Button {
    id: control
    Layout.fillWidth: true
    Layout.preferredHeight: 56
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 18

    property bool active: false

    background: Rectangle {
        radius: 12
        border.width: 1
        border.color: control.active ? "#a0a0a0" : "#888888"
        color: control.active ? "#e8e8e8" : "#c5c5c5"
    }

    contentItem: Text {
        text: control.text
        color: "#2d2d2d"
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
