import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: historyPage

    property var transactions: []
    property bool isLoading: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string selectedWallet: ""

    function refreshHistory() {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        if (selectedWallet === "") {
            if (errorLabel)
                errorLabel.text = "Select a wallet"
            return
        }
        if (walletBusy)
            return
        isLoading = true
        if (errorLabel)
            errorLabel.text = ""
        cliBridge.call("listtransactions", ["*", "50", "0", "true"], selectedWallet)
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
            if (errorLabel)
                errorLabel.text = errorMessage
        }
    }

    function refresh() {
        if (selectedWallet !== "")
            refreshHistory()
    }

    Connections {
        target: walletManager
        function onLoadedWalletsChanged() {
            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && historyWalletCombo.currentIndex < 0)
                historyWalletCombo.currentIndex = 0
        }
    }

    Component.onCompleted: {
        if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && historyWalletCombo.currentIndex < 0)
            historyWalletCombo.currentIndex = 0
        if (selectedWallet !== "")
            refreshHistory()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "Wallet:"; font.pixelSize: 14 }
            ComboBox {
                id: historyWalletCombo
                Layout.preferredWidth: 260
                Layout.preferredHeight: 36
                model: walletManager ? walletManager.wallets : []
                onActivated: {
                    if (walletManager && walletManager.wallets && index >= 0 && index < walletManager.wallets.length) {
                        selectedWallet = walletManager.wallets[index]
                        refreshHistory()
                    }
                }
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && walletManager && walletManager.wallets && currentIndex < walletManager.wallets.length) {
                        var w = walletManager.wallets[currentIndex]
                        if (w !== selectedWallet) {
                            selectedWallet = w
                            refreshHistory()
                        }
                    }
                }
                Component.onCompleted: {
                    if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && currentIndex < 0)
                        currentIndex = 0
                }
            }
            Item { Layout.fillWidth: true }
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
                enabled: settingsManager && settingsManager.qbitxCliPath !== "" && selectedWallet !== "" && !walletBusy
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
