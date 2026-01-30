import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: balancePage

    property var balances: ({})
    property bool isLoading: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: settingsManager ? (settingsManager.activeWallet !== "") : false
    
    // Safe property bindings to prevent undefined-to-bool assignments
    property bool hasBalancesData: balances !== undefined && typeof balances === "object" && balances !== null
    property bool hasMineData: hasBalancesData && balances.mine !== undefined && typeof balances.mine === "object"
    property bool hasWatchonlyData: hasBalancesData && balances.watchonly !== undefined && typeof balances.watchonly === "object"
    property bool showWatchonlyConfirmed: hasWatchonlyData && balances.watchonly.trusted !== undefined
    property bool showWatchonlyUnconfirmed: hasWatchonlyData && balances.watchonly.untrusted_pending !== undefined  
    property bool showWatchonlyImmature: hasWatchonlyData && balances.watchonly.immature !== undefined

    function refreshBalances() {
        if (!settingsManager || settingsManager.qbitxCliPath === "") {
            return
        }
        if (!settingsManager.activeWallet || settingsManager.activeWallet === "") {
            errorLabel.text = "No active wallet selected"
            return
        }
        if (walletBusy) {
            return  // Don't poll during wallet operations
        }
        isLoading = true
        errorLabel.text = ""
        cliBridge.call("getbalances", [], settingsManager.activeWallet)
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            isLoading = false
            // MONOTONIC RULE: ANY result format treated as "loaded but empty wallet"
            // Never clear or invalidate activeWallet based on balance data
            if (result !== undefined) {
                balances = (typeof result === "object" && result !== null) ? result : {}
            } else {
                // Unknown/undefined result = empty wallet, NOT unloaded wallet
                balances = {}
            }
            // Clear any previous errors
            if (errorLabel) {
                errorLabel.text = ""
            }
        }
        function onErrorOccurred(errorMessage) {
            isLoading = false
            // MONOTONIC RULE: Balance RPC errors NEVER invalidate activeWallet
            // getbalances failure = "loaded wallet with balance error", NOT "unloaded wallet"
            if (errorLabel) {
                errorLabel.text = errorMessage
            }
            // CRITICAL: Keep existing balances - RPC error is temporary, wallet is still loaded
            // Do not clear balances = {} or reset any wallet state
        }
    }

    Connections {
        target: settingsManager
        function onActiveWalletChanged() {
            // Refresh balances when active wallet changes
            if (settingsManager && settingsManager.activeWallet && settingsManager.activeWallet !== "") {
                // Clear any previous error when switching wallets
                if (errorLabel) {
                    errorLabel.text = ""
                }
                Qt.callLater(refreshBalances) // Use callLater to ensure proper timing
            } else {
                // Clear balances data when no active wallet
                balances = ({})
                if (errorLabel) {
                    errorLabel.text = ""
                }
            }
        }
    }

    Component.onCompleted: {
        if (hasWallet) {
            refreshBalances()
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
            text: "Balance"
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
                if (!settingsManager) return false
                if (!walletManager) return false  
                return settingsManager.qbitxCliPath !== "" && hasWallet && !walletBusy
            }
            onClicked: refreshBalances()
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

        GroupBox {
            Layout.fillWidth: true
            title: "Wallet Balances"

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: 20
                rowSpacing: 10

                Text { 
                    text: "Confirmed (Mine):"
                    font.bold: true
                }
                Text { 
                    text: (hasMineData && balances.mine.trusted !== undefined) 
                          ? balances.mine.trusted + " QBX" 
                          : "0 QBX"
                }

                Text { 
                    text: "Unconfirmed (Mine):"
                    font.bold: true
                }
                Text { 
                    text: (hasMineData && balances.mine.untrusted_pending !== undefined) 
                          ? balances.mine.untrusted_pending + " QBX" 
                          : "0 QBX"
                }

                Text { 
                    text: "Immature (Mine):"
                    font.bold: true
                }
                Text { 
                    text: (hasMineData && balances.mine.immature !== undefined) 
                          ? balances.mine.immature + " QBX" 
                          : "0 QBX"
                }

                Text { 
                    text: "Total (Mine):"
                    font.bold: true
                    font.pixelSize: 14
                }
                Text { 
                    text: {
                        if (!hasMineData) return "0 QBX"
                        var confirmed = (balances.mine.trusted !== undefined) ? balances.mine.trusted : 0
                        var unconfirmed = (balances.mine.untrusted_pending !== undefined) ? balances.mine.untrusted_pending : 0
                        var immature = (balances.mine.immature !== undefined) ? balances.mine.immature : 0
                        return (confirmed + unconfirmed + immature) + " QBX"
                    }
                    font.bold: true
                    font.pixelSize: 14
                    color: "#2196F3"
                }

                // Watchonly section (only show if present)
                Text { 
                    text: "Confirmed (Watchonly):"
                    font.bold: true
                    visible: showWatchonlyConfirmed
                }
                Text { 
                    text: showWatchonlyConfirmed ? (balances.watchonly.trusted + " QBX") : ""
                    visible: showWatchonlyConfirmed
                }

                Text { 
                    text: "Unconfirmed (Watchonly):"
                    font.bold: true
                    visible: showWatchonlyUnconfirmed
                }
                Text { 
                    text: showWatchonlyUnconfirmed ? (balances.watchonly.untrusted_pending + " QBX") : ""
                    visible: showWatchonlyUnconfirmed
                }

                Text { 
                    text: "Immature (Watchonly):"
                    font.bold: true
                    visible: showWatchonlyImmature
                }
                Text { 
                    text: showWatchonlyImmature ? (balances.watchonly.immature + " QBX") : ""
                    visible: showWatchonlyImmature
                }
            }
        }
    }
}
