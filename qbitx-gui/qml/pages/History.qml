import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: historyPage

    property var transactions: []
    property bool isLoading: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: settingsManager ? (settingsManager.activeWallet !== "") : false

    function refreshHistory() {
        if (!settingsManager || settingsManager.qbitxCliPath === "") {
            return
        }
        if (!settingsManager.activeWallet || settingsManager.activeWallet === "") {
            if (errorLabel) {
                errorLabel.text = "No active wallet selected"
            }
            return
        }
        if (walletBusy) {
            return  // Don't poll during wallet operations
        }
        isLoading = true
        if (errorLabel) {
            errorLabel.text = ""
        }
        cliBridge.call("listtransactions", ["*", "50", "0", "true"], settingsManager.activeWallet)
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            isLoading = false
            // MONOTONIC RULE: ANY result format = "loaded but empty wallet"
            // Empty transaction list does NOT mean wallet is unloaded
            if (Array.isArray(result)) {
                transactions = result  // Accept any array (including empty)
            } else {
                // Unknown format = empty history, NOT unloaded wallet
                transactions = []
                console.log("History: Non-array result treated as empty history:", typeof result)
            }
            if (errorLabel) {
                errorLabel.text = ""
            }
        }
        function onErrorOccurred(errorMessage) {
            isLoading = false
            // MONOTONIC RULE: Transaction RPC errors NEVER invalidate activeWallet
            // listtransactions failure = "loaded wallet with history error", NOT "unloaded wallet"
            if (errorLabel) {
                errorLabel.text = errorMessage
            }
            // CRITICAL: Keep existing transactions - RPC error is temporary, wallet is still loaded
        }
    }

    Connections {
        target: settingsManager
        function onActiveWalletChanged() {
            // Refresh history when active wallet changes
            if (settingsManager.activeWallet !== "") {
                Qt.callLater(refreshHistory) // Use callLater to ensure proper timing
            } else {
                transactions = []
                errorLabel.text = ""
            }
        }
    }

    Component.onCompleted: {
        if (hasWallet) {
            refreshHistory()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // No active wallet banner
        Rectangle {
            Layout.fillWidth: true
            height: 60
            color: "#fff3cd"
            border.color: "#ffc107"
            border.width: 1
            visible: !hasWallet
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: 15

                Text {
                    Layout.fillWidth: true
                    text: "No active wallet selected. Please load a wallet from the Wallets page."
                    color: "#856404"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
            }
        }

        Text {
            text: "Transaction History"
            font.pixelSize: 24
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#fff3cd"
            border.color: "#ffc107"
            border.width: 1
            visible: settingsManager ? (settingsManager.qbitxCliPath === "") : false

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

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "Refresh"
                enabled: {
                    if (!settingsManager || !walletManager) return false
                    return settingsManager.qbitxCliPath !== "" && hasWallet && !walletBusy
                }
                onClicked: refreshHistory()
            }
        }

        Text {
            id: errorLabel
            color: "red"
            visible: text !== undefined && text !== null && text !== ""
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignCenter
            running: isLoading
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                model: transactions
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 100
                    border.color: "#ccc"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "TXID: " + (modelData.txid || "N/A")
                                font.pixelSize: 12
                                font.family: "monospace"
                                Layout.fillWidth: true
                            }

                            Text {
                                text: (modelData.amount || 0) + " QBX"
                                font.pixelSize: 14
                                font.bold: true
                                color: (modelData.amount || 0) >= 0 ? "green" : "red"
                            }
                        }

                        Text {
                            text: "Category: " + (modelData.category || "N/A")
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Confirmations: " + (modelData.confirmations || 0)
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Time: " + (modelData.time ? new Date(modelData.time * 1000).toLocaleString() : "N/A")
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }
}
