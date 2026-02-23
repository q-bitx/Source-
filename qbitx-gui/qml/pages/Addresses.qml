import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0

Page {
    id: addressesPage

    property var addressList: []
    property bool isLoading: false
    property string lastGeneratedAddress: ""
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property string selectedWallet: ""
    property string lastStatusMessage: ""
    property string generateDialogChosenWallet: ""
    property var walletSummary: ({ confirmed: "0 QBX", unconfirmed: "0 QBX", immature: "0 QBX", total: "0 QBX" })

    // Table columns: numeric only (no "QBX"). Use for Confirmed / Unconfirmed / Immature / Total cells.
    function formatAmountNumber(v) {
        if (v === undefined || v === null || v === "") return "0"
        var s = (typeof v === "string") ? String(v).replace(/\s*QBX\s*$/gi, "").trim() : String(v)
        var n = parseFloat(s)
        return String(isNaN(n) ? 0 : n)
    }

    // Wallet Balances section only: returns "<number> QBX".
    function formatQbxAmount(v) {
        if (v === undefined || v === null || v === "") return "0 QBX"
        var s = (typeof v === "string") ? String(v).replace(/\s*QBX\s*$/i, "").trim() : String(v)
        var n = parseFloat(s)
        return (isNaN(n) ? 0 : n) + " QBX"
    }

    function refresh() {
        if (selectedWallet !== "")
            loadAddresses(selectedWallet)
    }

    function loadAddresses(wallet) {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        var w = (wallet && typeof wallet === "string") ? wallet.trim() : ""
        if (w === "") {
            if (errorLabel)
                errorLabel.text = "Select a wallet"
            addressList = []
            return
        }
        if (walletBusy)
            return
        isLoading = true
        if (errorLabel)
            errorLabel.text = ""
        addressList = []
        walletSummary = { confirmed: "0 QBX", unconfirmed: "0 QBX", immature: "0 QBX", total: "0 QBX" }

        // Single fast RPC: getaddressbalances (no per-address getaddressinfo/gettxout/listunspent)
        cliBridge.callNamedWithTag("getaddressbalances", {
            "minconf": 0,
            "include_unsafe": true
        }, w, w)
    }

    function applyAddressBalances(byAddress) {
        var list = []
        var sumConfirmed = 0, sumUnconfirmed = 0, sumImmature = 0
        if (Array.isArray(byAddress)) {
            for (var i = 0; i < byAddress.length; i++) {
                var bal = byAddress[i]
                if (bal && bal.address) {
                    var c = (bal.confirmed !== undefined) ? Number(bal.confirmed) : 0
                    var u = (bal.unconfirmed !== undefined) ? Number(bal.unconfirmed) : 0
                    var im = (bal.immature !== undefined) ? Number(bal.immature) : 0
                    sumConfirmed += c
                    sumUnconfirmed += u
                    sumImmature += im
                    list.push({
                        address: bal.address,
                        confirmed: c,
                        unconfirmed: u,
                        immature: im,
                        utxos: (bal.utxos !== undefined) ? bal.utxos : (bal.utxo_count !== undefined ? bal.utxo_count : 0)
                    })
                }
            }
        }
        addressList = list
        walletSummary = {
            confirmed: sumConfirmed + " QBX",
            unconfirmed: sumUnconfirmed + " QBX",
            immature: sumImmature + " QBX",
            total: (sumConfirmed + sumUnconfirmed + sumImmature) + " QBX"
        }
    }

    Connections {
        target: cliBridge
        function onSuccessWithTag(result, tag) {
            // Only apply if this response is for the currently selected wallet (avoid stale data on wallet switch)
            if (tag !== selectedWallet)
                return
            if (result && typeof result === "object" && result.by_address !== undefined) {
                applyAddressBalances(result.by_address)
                if (errorLabel) errorLabel.text = ""
            } else {
                applyAddressBalances([])
                if (errorLabel) errorLabel.text = ""
            }
            isLoading = false
        }
        function onErrorOccurredWithTag(errorMessage, tag) {
            if (tag !== selectedWallet)
                return
            isLoading = false
            addressList = []
            if (errorLabel)
                errorLabel.text = errorMessage
        }
        function onSuccess(result) {
            // getnewaddress (Generate PQ Address dialog) still uses untagged call
            if (typeof result === "string" || (result && result.address)) {
                var addr = (typeof result === "string") ? result : result.address
                lastGeneratedAddress = addr
                lastStatusMessage = "Generated address for wallet " + generateDialogChosenWallet + ": " + addr
                if (errorLabel) errorLabel.text = ""
                isLoading = false
                if (generateDialogChosenWallet === selectedWallet)
                    loadAddresses(selectedWallet)
            }
        }
        function onErrorOccurred(errorMessage) {
            // getnewaddress or other untagged call failed
            isLoading = false
            if (errorLabel)
                errorLabel.text = errorMessage
        }
    }

    Connections {
        target: walletManager
        function onLoadedWalletsChanged() {
            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && walletCombo.currentIndex < 0) {
                walletCombo.currentIndex = 0
            }
        }
    }

    Component.onCompleted: {
        if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && walletCombo.currentIndex < 0)
            walletCombo.currentIndex = 0
        if (selectedWallet !== "")
            loadAddresses(selectedWallet)
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
                id: walletCombo
                                Layout.preferredWidth: 280
                Layout.preferredHeight: 36
                model: walletManager ? walletManager.wallets : []
                currentIndex: -1
                onActivated: {
                    var w = walletManager && walletManager.wallets && walletManager.wallets[index] ? walletManager.wallets[index] : ""
                    selectedWallet = w
                    if (w !== "")
                        loadAddresses(w)
                }
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && walletManager && walletManager.wallets && currentIndex < walletManager.wallets.length) {
                        var w = walletManager.wallets[currentIndex]
                        if (w !== selectedWallet) {
                            selectedWallet = w
                            if (w !== "")
                                loadAddresses(w)
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
            text: "Addresses"
            font.pixelSize: 22
            font.bold: true
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
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

        StatusPanel {
            message: walletManager && walletManager.lastError !== "" ? walletManager.lastError : ""
            panelType: "error"
        }

        // Top action bar: buttons centered as a group
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 52
            spacing: 14

            Item { Layout.fillWidth: true }
            Button {
                text: "Generate PQ Address"
                Layout.minimumWidth: 180
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: !isLoading && settingsManager && settingsManager.qbitxCliPath !== "" && !walletBusy && walletManager && walletManager.wallets && walletManager.wallets.length > 0
                onClicked: generateWalletDialog.open()
                background: Rectangle {
                    radius: 10
                    border.width: 1
                    border.color: parent.pressed ? "#a0a0a0" : (parent.hovered ? "#999" : "#888")
                    color: parent.pressed ? "#d0d0d0" : (parent.hovered ? "#d5d5d5" : "#c5c5c5")
                }
                contentItem: Text {
                    text: parent.text
                    color: "#2d2d2d"
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: "Refresh"
                Layout.minimumWidth: 180
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: !isLoading && selectedWallet !== "" && settingsManager && settingsManager.qbitxCliPath !== "" && !walletBusy
                onClicked: loadAddresses(selectedWallet)
                background: Rectangle {
                    radius: 10
                    border.width: 1
                    border.color: parent.pressed ? "#a0a0a0" : (parent.hovered ? "#999" : "#888")
                    color: parent.pressed ? "#d0d0d0" : (parent.hovered ? "#d5d5d5" : "#c5c5c5")
                }
                contentItem: Text {
                    text: parent.text
                    color: "#2d2d2d"
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }
        }

        Dialog {
            id: generateWalletDialog
            title: "Generate PQ Address"
            modal: true
            standardButtons: Dialog.NoButton
            width: 320
            onOpened: {
                var list = walletManager ? walletManager.wallets : []
                for (var i = 0; i < list.length; i++) {
                    if (list[i] === selectedWallet) {
                        generateWalletList.currentIndex = i
                        return
                    }
                }
                if (list.length > 0)
                    generateWalletList.currentIndex = 0
                else
                    generateWalletList.currentIndex = -1
            }
            onAccepted: {
                var w = walletManager && walletManager.wallets && generateWalletList.currentIndex >= 0 && generateWalletList.currentIndex < walletManager.wallets.length
                    ? walletManager.wallets[generateWalletList.currentIndex] : ""
                if (w !== "") {
                    generateDialogChosenWallet = w
                    isLoading = true
                    if (errorLabel) errorLabel.text = ""
                    lastStatusMessage = ""
                    cliBridge.call("getnewaddress", ["", "pq"], w)
                }
            }

            contentItem: ColumnLayout {
                Label { text: "Select wallet to generate address for:"; font.pixelSize: 14 }
                ListView {
                    id: generateWalletList
                    Layout.preferredWidth: 280
                    Layout.preferredHeight: Math.min(200, Math.max(120, (walletManager ? walletManager.wallets : []).length * 44))
                    clip: true
                    model: walletManager ? walletManager.wallets : []
                    currentIndex: -1
                    highlight: Rectangle {
                        color: "#cce5ff"
                        radius: 4
                        border.color: "#99c9ff"
                        border.width: 1
                    }
                    highlightFollowsCurrentItem: true
                    delegate: ItemDelegate {
                        width: generateWalletList.width - 4
                        height: 40
                        text: modelData
                        font.pixelSize: 14
                        highlighted: generateWalletList.currentIndex === index
                        onClicked: generateWalletList.currentIndex = index
                        background: Rectangle {
                            color: parent.highlighted ? "#cce5ff" : (parent.hovered ? "#e8e8e8" : "transparent")
                            radius: 2
                        }
                    }
                }
            }

            footer: RowLayout {
                Item { Layout.fillWidth: true }
                Button {
                    text: "Cancel"
                    onClicked: generateWalletDialog.reject()
                }
                Button {
                    text: "OK"
                    enabled: generateWalletList.currentIndex >= 0 && walletManager && walletManager.wallets && generateWalletList.currentIndex < walletManager.wallets.length
                    onClicked: generateWalletDialog.accept()
                }
            }
        }

        Text {
            visible: lastStatusMessage !== ""
            text: lastStatusMessage
            color: "#0c5460"
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text {
            id: errorLabel
            color: "red"
            visible: text !== undefined && text !== null && text !== ""
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 8
            visible: isLoading
            BusyIndicator {
                running: isLoading
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
            }
            Label {
                text: "Loading..."
                font.pixelSize: 14
                color: "#555"
            }
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
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 180
                                text: "Address"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignLeft
                            }

                            Text {
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                                text: "Confirmed"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                                text: "Unconfirmed"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                                text: "Immature"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.preferredWidth: 70
                                Layout.maximumWidth: 70
                                text: "UTXOs"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.preferredWidth: 120
                                Layout.maximumWidth: 120
                                text: "Total"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.preferredWidth: 70
                                Layout.maximumWidth: 70
                                text: "Copy"
                                font.bold: true
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }
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
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 180
                                text: (modelData && modelData.address) ? modelData.address : "N/A"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                horizontalAlignment: Text.AlignLeft
                            }

                            Text {
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                                text: formatAmountNumber(modelData && modelData.confirmed)
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                            }

                            Text {
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                                text: formatAmountNumber(modelData && modelData.unconfirmed)
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                            }

                            Text {
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                                text: formatAmountNumber(modelData && modelData.immature)
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                            }

                            Text {
                                Layout.preferredWidth: 70
                                Layout.maximumWidth: 70
                                text: (modelData && (modelData.utxos !== undefined || modelData.utxo_count !== undefined))
                                      ? (modelData.utxos || modelData.utxo_count || 0)
                                      : 0
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                            }

                            Text {
                                Layout.preferredWidth: 120
                                Layout.maximumWidth: 120
                                text: formatAmountNumber(modelData ? ((modelData.confirmed || 0) + (modelData.unconfirmed || 0) + (modelData.immature || 0)) : 0)
                                font.pixelSize: 12
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                            }

                            Item {
                                Layout.preferredWidth: 70
                                Layout.maximumWidth: 70
                                Layout.alignment: Qt.AlignCenter

                                Button {
                                    anchors.centerIn: parent
                                    width: 54
                                    height: 24
                                    text: "Copy"
                                    font.pixelSize: 11
                                    enabled: !!(modelData && modelData.address)
                                    onClicked: {
                                        if (modelData && modelData.address) {
                                            copyHelper.text = modelData.address
                                            copyHelper.selectAll()
                                            copyHelper.copy()
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        border.width: 1
                                        border.color: parent.pressed ? "#a0a0a0" : (parent.hovered ? "#999" : "#888")
                                        color: parent.pressed ? "#d0d0d0" : (parent.hovered ? "#d5d5d5" : "#c5c5c5")
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#2d2d2d"
                                        font: parent.font
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        GroupBox {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 24
            title: "Wallet Balances"

            GridLayout {
                columns: 2
                columnSpacing: 20
                rowSpacing: 10
                Layout.fillWidth: true

                Text {
                    text: "Confirmed (Mine):"
                    font.pixelSize: 16
                }
                Text {
                    text: formatQbxAmount(walletSummary.confirmed)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                    Layout.fillWidth: true
                }

                Text {
                    text: "Unconfirmed (Mine):"
                    font.pixelSize: 16
                }
                Text {
                    text: formatQbxAmount(walletSummary.unconfirmed)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                    Layout.fillWidth: true
                }

                Text {
                    text: "Immature (Mine):"
                    font.pixelSize: 16
                }
                Text {
                    text: formatQbxAmount(walletSummary.immature)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                    Layout.fillWidth: true
                }

                Text {
                    text: "Total (Mine):"
                    font.pixelSize: 16
                }
                Text {
                    text: formatQbxAmount(walletSummary.total)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                    Layout.fillWidth: true
                }
            }
        }

        TextField {
            id: copyHelper
            visible: false
        }
    }
}
