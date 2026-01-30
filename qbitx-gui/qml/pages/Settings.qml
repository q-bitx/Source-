import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: settingsPage

    property bool isTestingConnection: false
    property string testResult: ""

    function testConnection() {
        if (settingsManager.qbitxCliPath === "") {
            testResult = "Error: qbitx-cli path not set"
            return
        }
        isTestingConnection = true
        testResult = ""
        cliBridge.call("getnetworkinfo")
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            isTestingConnection = false
            if (result.version !== undefined) {
                var version = result.version || "N/A"
                var subversion = result.subversion || "N/A"
                testResult = "✓ Connection successful! Version: " + version + " (" + subversion + ")"
            } else {
                testResult = "✓ Connection successful!"
            }
        }
        function onErrorOccurred(errorMessage) {
            isTestingConnection = false
            testResult = "✗ Error: " + errorMessage
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 20

        ColumnLayout {
            width: settingsPage.width - 40
            spacing: 20

            Text {
                text: "Settings"
                font.pixelSize: 24
                font.bold: true
            }

            GroupBox {
                Layout.fillWidth: true
                title: "qbitx-cli Configuration"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 15

                    TextField {
                        id: cliPathField
                        Layout.fillWidth: true
                        placeholderText: "qbitx-cli path"
                        text: settingsManager.qbitxCliPath
                        onEditingFinished: {
                            settingsManager.qbitxCliPath = text
                        }
                    }

                    TextField {
                        id: datadirField
                        Layout.fillWidth: true
                        placeholderText: "Data directory"
                        text: settingsManager.datadir
                        onEditingFinished: {
                            settingsManager.datadir = text
                        }
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "RPC Configuration"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 15

                    TextField {
                        id: rpcuserField
                        Layout.fillWidth: true
                        placeholderText: "RPC Username"
                        text: settingsManager.rpcuser
                        onEditingFinished: {
                            settingsManager.rpcuser = text
                        }
                    }

                    TextField {
                        id: rpcpasswordField
                        Layout.fillWidth: true
                        placeholderText: "RPC Password"
                        echoMode: TextInput.Password
                        text: settingsManager.rpcpassword
                        onEditingFinished: {
                            settingsManager.rpcpassword = text
                        }
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "Network Configuration"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 15

                    ComboBox {
                        id: networkCombo
                        Layout.fillWidth: true
                        model: ["main", "testnet", "regtest"]
                        currentIndex: {
                            var idx = model.indexOf(settingsManager.network)
                            return idx >= 0 ? idx : 0
                        }
                        onCurrentTextChanged: {
                            settingsManager.network = currentText
                        }
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "Wallet Configuration"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 15
                    
                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        color: "#f5f5f5"
                        border.color: "#ccc"
                        border.width: 1
                        radius: 4
                        
                        Text {
                            anchors.centerIn: parent
                            text: (settingsManager && settingsManager.activeWallet) ? settingsManager.activeWallet : "No active wallet"
                            color: (settingsManager && settingsManager.activeWallet) ? "black" : "#666"
                            font.italic: !(settingsManager && settingsManager.activeWallet)
                        }
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: "Active wallet is managed automatically. Use the Wallets page to load/create wallets."
                        font.pixelSize: 10
                        color: "#666"
                        wrapMode: Text.Wrap
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "Connection Test"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 15

                    Button {
                        text: "Test Connection"
                        enabled: !isTestingConnection
                        onClicked: testConnection()
                    }

                    BusyIndicator {
                        Layout.alignment: Qt.AlignCenter
                        running: isTestingConnection
                    }

                    Text {
                        text: testResult
                        color: testResult.startsWith("✓") ? "green" : (testResult.startsWith("✗") ? "red" : "black")
                        visible: testResult !== ""
                    }
                }
            }
        }
    }
}
