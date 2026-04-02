import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0

Page {
    id: dashboardPage

    property var blockchainInfo: ({})
    property var networkInfo: ({})
    property bool hasBlockchainInfo: false
    property bool hasNetworkInfo: false
    property string lastError: ""
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool cliAvailable: false

    function refresh() {
        if (walletBusy)
            return
        cliAvailable = settingsManager.checkCliAvailable()
        if (!cliAvailable) {
            lastError = settingsManager.lastCliCheckError
            if (logManager)
                logManager.append("ERROR", "Dashboard: " + lastError)
            return
        }
        lastError = ""
        cliBridge.call("getblockchaininfo")
        cliBridge.call("getnetworkinfo")
    }

    Timer {
        id: refreshTimer
        interval: 5000
        running: visible && !walletBusy
        repeat: true
        onTriggered: refresh()
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
            lastError = errorMessage
            if (logManager)
                logManager.append("ERROR", "Dashboard RPC: " + errorMessage)
            // Keep last known values; do not overwrite blockchainInfo/networkInfo
        }
    }

    Component.onCompleted: {
        refreshTimer.start()
        refresh()
    }

    ScrollView {
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.bottomMargin: 24

        ColumnLayout {
            width: dashboardPage.width - 48
            spacing: 24
            Layout.alignment: Qt.AlignHCenter

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Text {
                    text: "Dashboard"
                    font.pixelSize: 22
                    font.bold: true
                }
                Rectangle {
                    visible: lastError !== ""
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: 110
                    radius: 4
                    color: "#f8d7da"
                    border.color: "#dc3545"
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "Disconnected"
                        font.pixelSize: 12
                        color: "#721c24"
                    }
                }
            }

            StatusPanel {
                message: !cliAvailable ? "Configure qbitx-cli in Settings or set QBITX_CLI_PATH" : ""
                panelType: "warning"
            }

            StatusPanel {
                message: lastError !== "" ? ("Error: " + lastError) : ""
                panelType: "error"
            }

            GroupBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                Layout.alignment: Qt.AlignHCenter
                title: "Blockchain Info"
                font.pixelSize: 22
                font.bold: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2

                    Text { text: "Chain:"; font.pixelSize: 16 }
                    Text {
                        text: hasBlockchainInfo && blockchainInfo.chain !== undefined
                              ? blockchainInfo.chain
                              : (!cliAvailable ? "Configure settings" : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text { text: "Blocks:"; font.pixelSize: 16 }
                    Text {
                        text: hasBlockchainInfo && blockchainInfo.blocks !== undefined
                              ? blockchainInfo.blocks
                              : (!cliAvailable ? "Configure settings" : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text { text: "Headers:"; font.pixelSize: 16 }
                    Text {
                        text: hasBlockchainInfo && blockchainInfo.headers !== undefined
                              ? blockchainInfo.headers
                              : (!cliAvailable ? "Configure settings" : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text { text: "Verification Progress:"; font.pixelSize: 16 }
                    Text {
                        text: {
                            if (!cliAvailable) return "Configure settings"
                            if (!hasBlockchainInfo) return "—"
                            if (blockchainInfo.initialblockdownload === false) return "Synced"
                            var blocks = blockchainInfo.blocks
                            var headers = blockchainInfo.headers
                            if (blocks === undefined || headers === undefined || headers <= 0) return "—"
                            var pct = Math.min(100, Math.max(0, (blocks / headers) * 100))
                            return pct.toFixed(2) + "%"
                        }
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text { text: "Difficulty:"; font.pixelSize: 16 }
                    Text {
                        text: hasBlockchainInfo && blockchainInfo.difficulty !== undefined
                              ? blockchainInfo.difficulty
                              : (!cliAvailable ? "Configure settings" : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                Layout.alignment: Qt.AlignHCenter
                title: "Network Info"
                font.pixelSize: 22
                font.bold: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2

                    Text { text: "Version:"; font.pixelSize: 16 }
                    Text {
                        text: hasNetworkInfo && networkInfo.version !== undefined
                              ? networkInfo.version
                              : (!cliAvailable ? "Configure settings" : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text { text: "Connections:"; font.pixelSize: 16 }
                    Text {
                        text: hasNetworkInfo && networkInfo.connections !== undefined
                              ? networkInfo.connections
                              : (!cliAvailable ? "Configure settings" : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text { text: "Network Active:"; font.pixelSize: 16 }
                    Text {
                        text: !cliAvailable ? "Configure settings"
                              : (hasNetworkInfo ? (networkInfo.networkactive !== undefined ? (networkInfo.networkactive ? "Yes" : "No") : "—") : "—")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }
}
