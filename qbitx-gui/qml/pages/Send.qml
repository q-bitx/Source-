import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: sendPage

    property var addressBalances: []
    property bool isValidAddress: false
    property bool isLoading: false
    property string validationError: ""
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: settingsManager ? (settingsManager.activeWallet !== "") : false

    function validateAddress(address) {
        if (!settingsManager || settingsManager.qbitxCliPath === "") {
            return
        }
        if (!address || address.trim() === "") {
            isValidAddress = false
            validationError = ""
            return
        }
        isLoading = true
        cliBridge.call("validateaddress", [address])
    }

    function loadAddressBalances() {
        if (!settingsManager || settingsManager.qbitxCliPath === "") {
            return
        }
        if (!settingsManager.activeWallet || settingsManager.activeWallet === "") {
            return
        }
        if (walletBusy) {
            return  // Don't poll during wallet operations
        }
        cliBridge.callNamed("getaddressbalances", {
            "minconf": 0,
            "include_unsafe": true
        }, settingsManager.activeWallet)
    }

    function sendTransaction() {
        if (settingsManager.qbitxCliPath === "") {
            return
        }
        if (!isValidAddress) {
            errorLabel.text = "Please enter a valid address"
            return
        }
        if (fromAddressField.text === "") {
            errorLabel.text = "Please select a from address"
            return
        }
        if (amountField.text === "" || parseFloat(amountField.text) <= 0) {
            errorLabel.text = "Please enter a valid amount"
            return
        }

        isLoading = true
        errorLabel.text = ""
        var feePolicy = feePolicyCombo.currentText || "CONSERVATIVE"
        cliBridge.call("pqsendfrom", [
            fromAddressField.text,
            toAddressField.text,
            amountField.text,
            feePolicy
        ], settingsManager.activeWallet)
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            isLoading = false
            if (result.isvalid !== undefined) {
                isValidAddress = result.isvalid
                if (!isValidAddress) {
                    validationError = result.error || "Invalid address"
                } else {
                    validationError = ""
                }
            } else if (result instanceof Array) {
                // Handle legacy array format
                addressBalances = result
                // Auto-select first address with balance if none selected
                if (fromAddressField.text === "" && addressBalances.length > 0) {
                    fromAddressField.text = addressBalances[0].address || ""
                }
            } else if (result && typeof result === "object" && result.by_address !== undefined) {
                // Handle getaddressbalances object format: {by_address: [...]}
                addressBalances = (result.by_address instanceof Array) ? result.by_address : []
                // Auto-select first address with balance if none selected
                if (fromAddressField.text === "" && addressBalances.length > 0) {
                    fromAddressField.text = addressBalances[0].address || ""
                }
            } else {
                // Transaction result
                successLabel.text = "Transaction sent successfully!"
                successLabel.visible = true
                toAddressField.text = ""
                amountField.text = ""
            }
        }
        function onErrorOccurred(errorMessage) {
            isLoading = false
            // MONOTONIC RULE: Send/validation RPC errors NEVER affect activeWallet
            errorLabel.text = errorMessage
            // Keep existing address balances - validation error doesn't mean wallet unloaded
        }
    }

    Component.onCompleted: {
        if (hasWallet) {
            loadAddressBalances()
        }
    }

    Connections {
        target: settingsManager
        function onActiveWalletChanged() {
            // Refresh address balances when active wallet changes
            if (settingsManager && settingsManager.activeWallet && settingsManager.activeWallet !== "") {
                Qt.callLater(loadAddressBalances) // Use callLater to ensure proper timing
            } else {
                // Clear address balances when no active wallet
                addressBalances = []
            }
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
            text: "Send"
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

        GroupBox {
            Layout.fillWidth: true
            title: "Send Transaction"

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                Text {
                    text: "Active Wallet: " + (settingsManager.activeWallet || "None")
                }

                TextField {
                    id: toAddressField
                    Layout.fillWidth: true
                    placeholderText: "To Address"
                    onTextChanged: {
                        if (text.length > 10) {
                            validateAddress(text)
                        } else {
                            isValidAddress = false
                            validationError = ""
                        }
                    }
                }

                Text {
                    text: validationError || ""
                    color: "red"
                    visible: validationError !== undefined && validationError !== null && validationError !== ""
                }

                Text {
                    text: "✓ Valid Address"
                    color: "green"
                    visible: isValidAddress === true
                }

                ComboBox {
                    id: fromAddressCombo
                    Layout.fillWidth: true
                    model: addressBalances
                    textRole: "address"
                    onCurrentTextChanged: {
                        if (currentText) {
                            fromAddressField.text = currentText
                        }
                    }
                }

                TextField {
                    id: fromAddressField
                    Layout.fillWidth: true
                    placeholderText: "From Address (auto-selected)"
                    readOnly: true
                }

                TextField {
                    id: amountField
                    Layout.fillWidth: true
                    placeholderText: "Amount"
                    validator: DoubleValidator { bottom: 0 }
                }

                ComboBox {
                    id: feePolicyCombo
                    Layout.fillWidth: true
                    model: ["CONSERVATIVE", "ECONOMICAL", "NORMAL"]
                    currentIndex: 0
                }

                Button {
                    text: "Send"
                    enabled: {
                        if (!settingsManager || !walletManager) return false
                        return !isLoading && isValidAddress === true && amountField.text !== "" && hasWallet && !walletBusy
                    }
                    onClicked: sendTransaction()
                }

                BusyIndicator {
                    Layout.alignment: Qt.AlignCenter
                    running: isLoading
                }

        Text {
            id: errorLabel
            color: "red"
            visible: text !== undefined && text !== null && text !== ""
        }

        Text {
            id: successLabel
            color: "green"
            visible: false
        }
            }
        }
    }
}
