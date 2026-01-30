import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: addressesPage

    property var addressList: []
    property var addressBalancesMap: ({})
    property var fullAddressList: []
    property var addressBalancesList: []
    property bool isLoading: false
    property string lastGeneratedAddress: ""
    property int pendingRequests: 0
    property bool hasFullList: false
    property bool hasBalances: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: settingsManager ? (settingsManager.activeWallet !== "") : false

    function checkAndMerge() {
        if (pendingRequests === 0) {
            // MONOTONIC RULE: Merge whatever data we have, treat failures as "empty wallet"
            if (hasFullList || hasBalances) {
                mergeAddressData()  // Show partial or complete data
            } else {
                // Both RPC calls failed = "loaded wallet with empty addresses", NOT "unloaded wallet"
                addressList = []    // Show empty list, keep wallet active
            }
            isLoading = false
            // Individual error handlers manage errorLabel - never clear activeWallet
        }
    }

    function refreshAddresses() {
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
        errorLabel.text = ""
        pendingRequests = 2
        fullAddressList = []
        addressBalancesList = []
        hasFullList = false
        hasBalances = false
        
        // Call A: Get full address list (all addresses, even empty)
        cliBridge.callNamed("listreceivedbyaddress", {
            "minconf": 0,
            "include_empty": true,
            "include_watchonly": false
        }, settingsManager.activeWallet)
        
        // Call B: Get address balances (funded addresses only)
        // Use -named to ensure proper type conversion
        cliBridge.callNamed("getaddressbalances", {
            "minconf": 0,
            "include_unsafe": true
        }, settingsManager.activeWallet)
    }

    function mergeAddressData() {
        // Create a map of balances by address
        var balancesMap = {}
        if (Array.isArray(addressBalancesList)) {
            for (var i = 0; i < addressBalancesList.length; i++) {
                var bal = addressBalancesList[i]
                if (bal && bal.address) {
                    balancesMap[bal.address] = bal
                }
            }
        }
        
        // Merge: start with full list, add balance data
        var merged = []
        if (Array.isArray(fullAddressList) && fullAddressList.length > 0) {
            // We have full address list - merge with balances
            for (var j = 0; j < fullAddressList.length; j++) {
                var addr = fullAddressList[j]
                var address = (addr && addr.address) ? addr.address : ""
                if (address) {
                    var balanceData = balancesMap[address] || {}
                    merged.push({
                        address: address,
                        confirmed: (balanceData.confirmed !== undefined) ? balanceData.confirmed : 0,
                        unconfirmed: (balanceData.unconfirmed !== undefined) ? balanceData.unconfirmed : 0,
                        immature: (balanceData.immature !== undefined) ? balanceData.immature : 0,
                        utxos: (balanceData.utxos !== undefined) ? balanceData.utxos : (balanceData.utxo_count !== undefined ? balanceData.utxo_count : 0)
                    })
                }
            }
        }
        
        // Also include any addresses from balances that weren't in the full list
        // (or if we don't have full list, use balances as the source)
        if (!hasFullList || merged.length === 0) {
            // Fallback: use balances as address list
            for (var addrKey in balancesMap) {
                var bal = balancesMap[addrKey]
                if (bal) {
                    merged.push({
                        address: addrKey,
                        confirmed: (bal.confirmed !== undefined) ? bal.confirmed : 0,
                        unconfirmed: (bal.unconfirmed !== undefined) ? bal.unconfirmed : 0,
                        immature: (bal.immature !== undefined) ? bal.immature : 0,
                        utxos: (bal.utxos !== undefined) ? bal.utxos : (bal.utxo_count !== undefined ? bal.utxo_count : 0)
                    })
                }
            }
        } else {
            // Add any addresses from balances that weren't in full list
            for (var addrKey2 in balancesMap) {
                var found = false
                for (var k = 0; k < merged.length; k++) {
                    if (merged[k].address === addrKey2) {
                        found = true
                        break
                    }
                }
                if (!found) {
                    var bal2 = balancesMap[addrKey2]
                    if (bal2) {
                        merged.push({
                            address: addrKey2,
                            confirmed: (bal2.confirmed !== undefined) ? bal2.confirmed : 0,
                            unconfirmed: (bal2.unconfirmed !== undefined) ? bal2.unconfirmed : 0,
                            immature: (bal2.immature !== undefined) ? bal2.immature : 0,
                            utxos: (bal2.utxos !== undefined) ? bal2.utxos : (bal2.utxo_count !== undefined ? bal2.utxo_count : 0)
                        })
                    }
                }
            }
        }
        
        addressList = merged
    }

    function generatePQAddress() {
        if (!settingsManager || settingsManager.qbitxCliPath === "") {
            return
        }
        if (!settingsManager.activeWallet || settingsManager.activeWallet === "") {
            if (errorLabel) {
                errorLabel.text = "No active wallet selected"
            }
            return
        }
        isLoading = true
        errorLabel.text = ""
        cliBridge.call("getnewaddress", ["", "pq"], settingsManager.activeWallet)
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            // MONOTONIC RULE: ALL RPC responses treated as "loaded but empty wallet"
            // NEVER invalidate activeWallet based on address data
            if (Array.isArray(result)) {
                if (result.length > 0 && result[0].address && (result[0].amount !== undefined || result[0].confirmations !== undefined)) {
                    // Valid listreceivedbyaddress response
                    fullAddressList = result
                    hasFullList = true
                    pendingRequests--
                } else {
                    // Empty but valid array = empty wallet (NOT unloaded wallet)
                    fullAddressList = result
                    hasFullList = true
                    pendingRequests--
                }
                if (errorLabel) errorLabel.text = ""
                checkAndMerge()
            } else if (result && typeof result === "object" && result.by_address !== undefined) {
                // Valid getaddressbalances response (empty arrays are valid)
                addressBalancesList = (Array.isArray(result.by_address)) ? result.by_address : []
                hasBalances = true
                pendingRequests--
                if (errorLabel) errorLabel.text = ""
                checkAndMerge()
            } else if (typeof result === "string" || (result && result.address)) {
                // New address generation
                lastGeneratedAddress = (typeof result === "string") ? result : result.address
                isLoading = false
                refreshAddresses()
            } else {
                // CRITICAL: Unknown/unexpected format = "loaded but empty", NOT "wallet invalid"
                // Defensive: treat as empty arrays
                if (!hasFullList) {
                    fullAddressList = []
                    hasFullList = true
                }
                if (!hasBalances) {
                    addressBalancesList = []
                    hasBalances = true
                }
                pendingRequests--
                if (pendingRequests <= 0) {
                    isLoading = false
                }
                console.log("Addresses: Unexpected format treated as empty wallet:", typeof result)
                // Still complete the merge - don't break the flow
                checkAndMerge()
            }
        }
        function onErrorOccurred(errorMessage) {
            // MONOTONIC RULE: RPC errors NEVER affect wallet state
            // Address RPC failures = "loaded wallet with address errors", NOT "unloaded wallet"
            pendingRequests--
            if (errorMessage.indexOf("listreceivedbyaddress") >= 0 || errorMessage.indexOf("Unknown method") >= 0 || errorMessage.indexOf("Method not found") >= 0) {
                hasFullList = false
                if (pendingRequests <= 0) {
                    checkAndMerge()  // Merge what we have - show partial data
                }
            } else if (errorMessage.indexOf("getaddressbalances") >= 0) {
                hasBalances = false
                if (pendingRequests <= 0) {
                    checkAndMerge()  // Show what we can, keep wallet active
                }
            } else {
                // Unknown RPC error - treat as temporary issue, keep wallet active
                if (pendingRequests <= 0) {
                    checkAndMerge()
                }
            }
            if (pendingRequests <= 0) {
                isLoading = false
            }
            // Show error but NEVER clear activeWallet
            if (errorLabel) {
                errorLabel.text = errorMessage
            }
        }
    }

    Connections {
        target: settingsManager
        function onActiveWalletChanged() {
            // Refresh addresses when active wallet changes
            if (settingsManager && settingsManager.activeWallet && settingsManager.activeWallet !== "") {
                Qt.callLater(refreshAddresses) // Use callLater to ensure proper timing
            } else {
                // Clear address list when no active wallet
                addressList = []
                if (errorLabel) {
                    errorLabel.text = ""
                }
            }
        }
    }

    Component.onCompleted: {
        if (hasWallet) {
            refreshAddresses()
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
            text: "Addresses"
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
            spacing: 10

            Button {
                text: "Generate PQ Address"
                enabled: {
                    if (!settingsManager || !walletManager) return false
                    return !isLoading && hasWallet && settingsManager.qbitxCliPath !== "" && !walletBusy
                }
                onClicked: generatePQAddress()
            }

            Button {
                text: "Refresh"
                enabled: {
                    if (!settingsManager || !walletManager) return false
                    return !isLoading && hasWallet && settingsManager.qbitxCliPath !== "" && !walletBusy
                }
                onClicked: refreshAddresses()
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
            Layout.fillHeight: true
            title: "Address Balances"

            ScrollView {
                anchors.fill: parent

                ListView {
                    id: addressListView
                    model: addressList
                    clip: true

                    header: Rectangle {
                        width: addressListView.width
                        height: 30
                        color: "#e0e0e0"
                        border.color: "#ccc"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 5

                            Text {
                                Layout.preferredWidth: 300
                                text: "Address"
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: "Confirmed"
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: "Unconfirmed"
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: "Immature"
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: "UTXOs"
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: "Total"
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }

                    delegate: Rectangle {
                        width: addressListView.width
                        height: 50
                        color: {
                            if (modelData && modelData.address && lastGeneratedAddress && modelData.address === lastGeneratedAddress) {
                                return "#e8f5e9"
                            }
                            return (index % 2 === 0) ? "#f5f5f5" : "white"
                        }
                        border.color: "#ddd"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 5

                            Text {
                                Layout.preferredWidth: 300
                                text: (modelData && modelData.address) ? modelData.address : "N/A"
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: (modelData && modelData.confirmed !== undefined) ? modelData.confirmed + " QBX" : "0 QBX"
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: (modelData && modelData.unconfirmed !== undefined) ? modelData.unconfirmed + " QBX" : "0 QBX"
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: (modelData && modelData.immature !== undefined) ? modelData.immature + " QBX" : "0 QBX"
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 80
                                text: (modelData && (modelData.utxos !== undefined || modelData.utxo_count !== undefined)) 
                                      ? (modelData.utxos || modelData.utxo_count || 0) 
                                      : 0
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.preferredWidth: 100
                                text: {
                                    if (!modelData) return "0 QBX"
                                    var confirmed = (modelData.confirmed !== undefined) ? modelData.confirmed : 0
                                    var unconfirmed = (modelData.unconfirmed !== undefined) ? modelData.unconfirmed : 0
                                    var immature = (modelData.immature !== undefined) ? modelData.immature : 0
                                    return (confirmed + unconfirmed + immature) + " QBX"
                                }
                                font.pixelSize: 12
                                font.bold: true
                            }

                            Button {
                                text: "Copy"
                                Layout.preferredWidth: 60
                                enabled: !!(modelData && modelData.address)
                                onClicked: {
                                    if (modelData && modelData.address) {
                                        copyHelper.text = modelData.address
                                        copyHelper.selectAll()
                                        copyHelper.copy()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        TextField {
            id: copyHelper
            visible: false
        }
    }
}
