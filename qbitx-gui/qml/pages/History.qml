import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0
import "../theme" 1.0

Page {
    id: historyPage
    background: Rectangle { color: "transparent" }

    property var transactions: []
    property bool isLoading: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string selectedWallet: ""

    function transactionTimestamp(tx) {
        if (!tx)
            return 0
        return tx.time || tx.blocktime || 0
    }

    function sortTransactionsNewestFirst(list) {
        if (!Array.isArray(list))
            return []
        var sorted = list.slice()
        sorted.sort(function(a, b) {
            return transactionTimestamp(b) - transactionTimestamp(a)
        })
        return sorted
    }

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
            if (Array.isArray(result)) {
                transactions = sortTransactionsNewestFirst(result)
            } else {
                transactions = []
            }
            if (errorLabel)
                errorLabel.text = ""
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

    QbxPageLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            QbxSectionTitle { text: "Transaction History" }
            Item { Layout.fillWidth: true }
            QbxButton {
                text: "Refresh"
                compact: true
                enabled: settingsManager && settingsManager.qbitxCliPath !== "" && selectedWallet !== "" && !walletBusy
                onClicked: refreshHistory()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "Wallet"; color: QbxTheme.textSecondary; font.pixelSize: 13 }
            QbxComboBox {
                id: historyWalletCombo
                Layout.preferredWidth: 280
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
        }

        StatusPanel {
            message: settingsManager && settingsManager.qbitxCliPath === "" ? "Configure qbitx-cli in Settings" : ""
            panelType: "warning"
        }

        StatusPanel {
            message: errorLabel.text
            panelType: "error"
        }

        RowLayout {
            visible: isLoading
            spacing: 8
            BusyIndicator { running: isLoading; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Label { text: "Loading…"; color: QbxTheme.textMuted; font.pixelSize: 13 }
        }

        QbxCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Recent transactions"

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(200, historyList.contentHeight)
                id: historyList
                model: transactions
                clip: true
                spacing: 8

                delegate: Rectangle {
                    width: historyList.width
                    height: 88
                    radius: QbxTheme.radiusSmall
                    color: index % 2 === 0 ? QbxTheme.bgInput : Qt.darker(QbxTheme.bgInput, 1.05)
                    border.color: QbxTheme.border
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.txid || "N/A"
                                font.pixelSize: 11
                                font.family: "Consolas,Courier New,monospace"
                                color: QbxTheme.textSecondary
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: (modelData.amount || 0) + " QBX"
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: (modelData.amount || 0) >= 0 ? QbxTheme.success : QbxTheme.error
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16
                            Text {
                                text: "Category: " + (modelData.category || "—")
                                font.pixelSize: 12
                                color: QbxTheme.textMuted
                            }
                            Text {
                                text: "Confirmations: " + (modelData.confirmations || 0)
                                font.pixelSize: 12
                                color: QbxTheme.textMuted
                            }
                            Text {
                                text: modelData.time ? new Date(modelData.time * 1000).toLocaleString() : "—"
                                font.pixelSize: 12
                                color: QbxTheme.textMuted
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: selectedWallet === "" ? "Select a wallet" : "No transactions yet"
                    visible: transactions.length === 0 && !isLoading
                    color: QbxTheme.textMuted
                    font.pixelSize: 14
                }
            }
        }
    }

    Text {
        id: errorLabel
        visible: false
    }
}
