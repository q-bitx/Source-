import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0
import "../theme" 1.0

Page {
    id: dashboardPage
    background: Rectangle { color: "transparent" }

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
        if (!rpcBootstrap || !rpcBootstrap.rpcReady)
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

    function syncStatusText() {
        if (!cliAvailable) return "Configure settings"
        if (!hasBlockchainInfo) return "—"
        if (blockchainInfo.initialblockdownload === false) return "Synced"
        var blocks = blockchainInfo.blocks
        var headers = blockchainInfo.headers
        if (blocks === undefined || headers === undefined || headers <= 0) return "Syncing…"
        var pct = Math.min(100, Math.max(0, (blocks / headers) * 100))
        return "Syncing " + pct.toFixed(1) + "%"
    }

    Timer {
        id: refreshTimer
        interval: 5000
        running: visible && !walletBusy && rpcBootstrap && rpcBootstrap.rpcReady
        repeat: true
        onTriggered: refresh()
    }

    Connections {
        target: rpcBootstrap
        function onRpcReadyChanged() {
            if (rpcBootstrap && rpcBootstrap.rpcReady)
                refresh()
        }
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            if (result.chain !== undefined || result.blockchain !== undefined || result.blocks !== undefined) {
                blockchainInfo = result
                hasBlockchainInfo = true
                lastError = ""
            } else if (result.version !== undefined || result.subversion !== undefined) {
                networkInfo = result
                hasNetworkInfo = true
                lastError = ""
            }
        }
        function onErrorOccurred(errorMessage) {
            lastError = errorMessage
            if (logManager)
                logManager.append("ERROR", "Dashboard RPC: " + errorMessage)
        }
    }

    Component.onCompleted: {
        if (rpcBootstrap && rpcBootstrap.rpcReady)
            refresh()
    }

    QbxPageLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            QbxSectionTitle { text: "Dashboard" }

            Item { Layout.fillWidth: true }

            Rectangle {
                visible: lastError !== ""
                height: 26
                width: disconnectedLabel.implicitWidth + 20
                radius: QbxTheme.radiusSmall
                color: Qt.rgba(QbxTheme.error.r, QbxTheme.error.g, QbxTheme.error.b, 0.15)
                border.color: QbxTheme.error
                border.width: 1

                Text {
                    id: disconnectedLabel
                    anchors.centerIn: parent
                    text: "Disconnected"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: QbxTheme.error
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Layout.preferredHeight: QbxTheme.dashboardCardHeight

            QbxCard {
                id: blockchainCard
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
                Layout.preferredWidth: 1
                Layout.preferredHeight: QbxTheme.dashboardCardHeight
                title: "Blockchain Info"

                QbxInfoRow {
                    label: "Chain"
                    value: hasBlockchainInfo && blockchainInfo.chain !== undefined
                           ? blockchainInfo.chain
                           : (!cliAvailable ? "Configure settings" : "—")
                    highlight: true
                }
                QbxInfoRow {
                    label: "Blocks"
                    value: hasBlockchainInfo && blockchainInfo.blocks !== undefined
                           ? String(blockchainInfo.blocks)
                           : (!cliAvailable ? "Configure settings" : "—")
                }
                QbxInfoRow {
                    label: "Headers"
                    value: hasBlockchainInfo && blockchainInfo.headers !== undefined
                           ? String(blockchainInfo.headers)
                           : (!cliAvailable ? "Configure settings" : "—")
                }
                QbxInfoRow {
                    label: "Sync status"
                    value: syncStatusText()
                    highlight: syncStatusText() === "Synced"
                }
                QbxInfoRow {
                    label: "Difficulty"
                    value: hasBlockchainInfo && blockchainInfo.difficulty !== undefined
                           ? String(blockchainInfo.difficulty)
                           : (!cliAvailable ? "Configure settings" : "—")
                }
            }

            QbxCard {
                id: networkCard
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
                Layout.preferredWidth: 1
                Layout.preferredHeight: QbxTheme.dashboardCardHeight
                title: "Network Info"

                QbxInfoRow {
                    label: "Version"
                    value: hasNetworkInfo && networkInfo.version !== undefined
                           ? String(networkInfo.version)
                           : (!cliAvailable ? "Configure settings" : "—")
                }
                QbxInfoRow {
                    label: "Connections"
                    value: hasNetworkInfo && networkInfo.connections !== undefined
                           ? String(networkInfo.connections)
                           : (!cliAvailable ? "Configure settings" : "—")
                    highlight: hasNetworkInfo && networkInfo.connections > 0
                }
                QbxInfoRow {
                    label: "Network active"
                    value: !cliAvailable ? "Configure settings"
                          : (hasNetworkInfo ? (networkInfo.networkactive !== undefined ? (networkInfo.networkactive ? "Yes" : "No") : "—") : "—")
                }
            }
        }
    }
}
