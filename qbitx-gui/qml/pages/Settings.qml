import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: settingsPage

    function refresh() {
        if (settingsManager.useAutoDetectCli)
            settingsManager.autoDetectCliPath()
    }

    Component.onCompleted: {
        if (settingsManager.useAutoDetectCli)
            settingsManager.autoDetectCliPath()
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(720, parent ? parent.width - 48 : 720)
        spacing: 20

        Text {
            text: "Settings"
            font.pixelSize: 22
            font.bold: true
            Layout.fillWidth: true
        }

        GroupBox {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            title: "qbitx-cli"

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        id: autoDetectCheck
                        text: "Auto-detect path"
                        checked: settingsManager.useAutoDetectCli
                        onCheckedChanged: {
                            settingsManager.useAutoDetectCli = checked
                            if (checked)
                                settingsManager.autoDetectCliPath()
                        }
                    }
                }

                Label { text: "Path:"; font.pixelSize: 13 }
                TextField {
                    id: cliPathField
                    Layout.fillWidth: true
                    placeholderText: "qbitx-cli path"
                    readOnly: settingsManager.useAutoDetectCli
                    text: settingsManager.qbitxCliPath
                    onEditingFinished: {
                        if (!settingsManager.useAutoDetectCli)
                            settingsManager.qbitxCliPath = text
                    }
                }
                Text {
                    visible: settingsManager.useAutoDetectCli && settingsManager.qbitxCliPath === ""
                    text: "No qbitx-cli found. Enable manual override and set path."
                    font.pixelSize: 12
                    color: "#666"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Label { text: "Data directory (optional):"; font.pixelSize: 13 }
                TextField {
                    id: datadirField
                    Layout.fillWidth: true
                    placeholderText: "Leave empty for default"
                    text: settingsManager.datadir
                    onEditingFinished: settingsManager.datadir = text
                }
            }
        }

        GroupBox {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            title: "Network"

            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                ComboBox {
                    id: networkCombo
                    Layout.fillWidth: true
                    model: ["main", "testnet", "regtest"]
                    currentIndex: {
                        var idx = model.indexOf(settingsManager.network)
                        return idx >= 0 ? idx : 0
                    }
                    onCurrentTextChanged: settingsManager.network = currentText
                }
            }
        }
    }

    Binding {
        target: cliPathField
        property: "text"
        value: settingsManager.qbitxCliPath
        when: settingsManager.useAutoDetectCli
    }
}
