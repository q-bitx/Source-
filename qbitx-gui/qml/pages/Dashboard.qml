import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: dashboardPage

    property var blockchainInfo: ({})
    property var networkInfo: ({})
    property bool hasBlockchainInfo: false
    property bool hasNetworkInfo: false
    property string lastError: ""
    property bool walletBusy: walletManager ? walletManager.walletBusy : false

    Timer {
        id: refreshTimer
        interval: 5000 // 5 seconds
        running: settingsManager.qbitxCliPath !== "" && visible && !walletBusy
        repeat: true
        onTriggered: {
            if (settingsManager.qbitxCliPath !== "" && !walletBusy) {
                cliBridge.call("getblockchaininfo")
                cliBridge.call("getnetworkinfo")
            }
        }
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            // Try to determine which call succeeded based on result structure
            if (result.chain !== undefined || result.blockchain !== undefined || result.blocks !== undefined) {
                // This is blockchaininfo
                blockchainInfo = result
                hasBlockchainInfo = true
                lastError = ""
            } else if (result.version !== undefined || result.subversion !== undefined) {
                // This is networkinfo
                networkInfo = result
                hasNetworkInfo = true
                lastError = ""
            }
        }
        function onErrorOccurred(errorMessage) {
            console.log("Error:", errorMessage)
            lastError = errorMessage
            // Keep last known values, don't reset to N/A
        }
    }

    Component.onCompleted: {
        refreshTimer.start()
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 20

        ColumnLayout {
            width: dashboardPage.width - 40
            spacing: 20

            Text {
                text: "Dashboard"
                font.pixelSize: 24
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 50
                color: "#fff3cd"
                border.color: "#ffc107"
                border.width: 1
                visible: settingsManager.qbitxCliPath === ""

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    Text {
                        Layout.fillWidth: true
                        text: "Configure qbitx-cli in Settings"
                        color: "#856404"
                        font.pixelSize: 14
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 40
                color: "#f8d7da"
                border.color: "#dc3545"
                border.width: 1
                visible: lastError !== ""

                Text {
                    anchors.fill: parent
                    anchors.margins: 10
                    text: "Error: " + lastError
                    color: "#721c24"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "Blockchain Info"

                GridLayout {
                    anchors.fill: parent
                    columns: 2

                    Text { text: "Chain:" }
                    Text { 
                        text: hasBlockchainInfo && blockchainInfo.chain !== undefined 
                              ? blockchainInfo.chain 
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Blocks:" }
                    Text { 
                        text: hasBlockchainInfo && blockchainInfo.blocks !== undefined 
                              ? blockchainInfo.blocks 
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Headers:" }
                    Text { 
                        text: hasBlockchainInfo && blockchainInfo.headers !== undefined 
                              ? blockchainInfo.headers 
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Verification Progress:" }
                    Text { 
                        text: hasBlockchainInfo && blockchainInfo.verificationprogress !== undefined
                              ? (blockchainInfo.verificationprogress * 100).toFixed(2) + "%"
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Difficulty:" }
                    Text { 
                        text: hasBlockchainInfo && blockchainInfo.difficulty !== undefined
                              ? blockchainInfo.difficulty
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Network Active:" }
                    Text { 
                        text: hasBlockchainInfo && blockchainInfo.networkactive !== undefined
                              ? (blockchainInfo.networkactive ? "Yes" : "No")
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: "Network Info"

                GridLayout {
                    anchors.fill: parent
                    columns: 2

                    Text { text: "Version:" }
                    Text { 
                        text: hasNetworkInfo && networkInfo.version !== undefined
                              ? networkInfo.version
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Subversion:" }
                    Text { 
                        text: hasNetworkInfo && networkInfo.subversion !== undefined
                              ? networkInfo.subversion
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Connections:" }
                    Text { 
                        text: hasNetworkInfo && networkInfo.connections !== undefined
                              ? networkInfo.connections
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }

                    Text { text: "Network Active:" }
                    Text { 
                        text: hasNetworkInfo && networkInfo.networkactive !== undefined
                              ? (networkInfo.networkactive ? "Yes" : "No")
                              : (settingsManager.qbitxCliPath === "" ? "Configure settings" : "N/A")
                    }
                }
            }
        }
    }
}
