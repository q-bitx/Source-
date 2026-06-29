import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0
import "../theme" 1.0

Page {
    id: settingsPage
    background: Rectangle { color: "transparent" }

    function refresh() {
        if (settingsManager.useAutoDetectCli)
            settingsManager.autoDetectCliPath()
    }

    Component.onCompleted: {
        if (settingsManager.useAutoDetectCli)
            settingsManager.autoDetectCliPath()
    }

    QbxPageLayout {
        anchors.fill: parent

        QbxSectionTitle { text: "Settings" }

        QbxCard {
            Layout.fillWidth: true
            title: "qbitx-cli"

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12

                CheckBox {
                    id: autoDetectCheck
                    text: "Auto-detect path"
                    checked: settingsManager.useAutoDetectCli
                    onCheckedChanged: {
                        settingsManager.useAutoDetectCli = checked
                        if (checked)
                            settingsManager.autoDetectCliPath()
                    }
                    contentItem: Text {
                        text: parent.text
                        color: QbxTheme.textPrimary
                        font.pixelSize: 13
                        leftPadding: parent.indicator.width + parent.spacing
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Label { text: "Path"; color: QbxTheme.textSecondary; font.pixelSize: 13 }
                QbxTextField {
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
                    text: "No qbitx-cli found. Disable auto-detect and set the path manually."
                    font.pixelSize: 12
                    color: QbxTheme.warning
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Label { text: "Data directory (optional)"; color: QbxTheme.textSecondary; font.pixelSize: 13 }
                QbxTextField {
                    id: datadirField
                    Layout.fillWidth: true
                    placeholderText: "Leave empty for default"
                    text: settingsManager.datadir
                    onEditingFinished: settingsManager.datadir = text
                }
            }
        }

        QbxCard {
            Layout.fillWidth: true
            title: "Network"

            QbxComboBox {
                id: networkCombo
                Layout.fillWidth: true
                Layout.maximumWidth: 280
                model: ["main", "testnet", "regtest"]
                currentIndex: {
                    var idx = model.indexOf(settingsManager.network)
                    return idx >= 0 ? idx : 0
                }
                onCurrentTextChanged: settingsManager.network = currentText
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
