import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0
import "../theme" 1.0

Page {
    id: addressesPage
    background: Rectangle { color: "transparent" }

    property var addressList: []
    property bool isLoading: false
    property string lastGeneratedAddress: ""
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property string selectedWallet: ""
    property string lastStatusMessage: ""
    property string generateDialogChosenWallet: ""
    property var walletSummary: ({ confirmed: "0 QBX", unconfirmed: "0 QBX", immature: "0 QBX", total: "0 QBX" })

    // Dual-RPC load state: listreceivedbyaddress (all receive addrs) + getaddressbalances (UTXO balances)
    property var _recvListResult: null
    property var _balListResult: null
    property bool _recvDone: false
    property bool _balDone: false
    property string _loadWalletTag: ""

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
        else if (walletManager && walletManager.wallets && walletManager.wallets.length > 0)
            walletCombo.currentIndex = 0
    }

    function syncWalletFromCombo() {
        if (walletCombo.currentIndex >= 0
                && walletManager
                && walletManager.wallets
                && walletCombo.currentIndex < walletManager.wallets.length) {
            var w = walletManager.wallets[walletCombo.currentIndex]
            if (w !== selectedWallet) {
                selectedWallet = w
                loadAddresses(w)
            } else if (addressList.length === 0 && !isLoading) {
                loadAddresses(w)
            }
        } else if (walletCombo.currentIndex < 0) {
            selectedWallet = ""
            addressList = []
        }
    }

    function resetLoadState(walletTag) {
        _recvListResult = null
        _balListResult = null
        _recvDone = false
        _balDone = false
        _loadWalletTag = walletTag
    }

    function insertAddressImmediate(addr) {
        if (!addr || typeof addr !== "string")
            return
        for (var i = 0; i < addressList.length; i++) {
            if (addressList[i].address === addr)
                return
        }
        var list = addressList.slice()
        list.unshift({
            address: addr,
            confirmed: 0,
            unconfirmed: 0,
            immature: 0,
            utxos: 0
        })
        addressList = list
    }

    function parseBalanceFields(bal) {
        if (!bal || typeof bal !== "object")
            return { confirmed: 0, unconfirmed: 0, immature: 0, utxos: 0 }

        function toNum(v) {
            if (v === undefined || v === null || v === "")
                return 0
            var s = (typeof v === "string") ? String(v).replace(/\s*QBX\s*$/gi, "").trim() : String(v)
            var n = parseFloat(s)
            return isNaN(n) ? 0 : n
        }

        var c = toNum(bal.confirmed)
        var u = toNum(bal.unconfirmed)
        var im = toNum(bal.immature)
        return {
            confirmed: c,
            unconfirmed: u,
            immature: im,
            utxos: bal.utxos !== undefined ? bal.utxos : (bal.utxo_count !== undefined ? bal.utxo_count : 0)
        }
    }

    function mergeAndApplyAddresses(receiveList, balanceList, balanceTotals) {
        var balanceMap = {}
        if (Array.isArray(balanceList)) {
            for (var i = 0; i < balanceList.length; i++) {
                var b = balanceList[i]
                if (b && b.address)
                    balanceMap[b.address] = b
            }
        }

        var merged = []
        var seen = {}

        function pushEntry(addr, bal) {
            var parsed = parseBalanceFields(bal)
            merged.push({
                address: addr,
                confirmed: parsed.confirmed,
                unconfirmed: parsed.unconfirmed,
                immature: parsed.immature,
                utxos: parsed.utxos
            })
        }

        if (Array.isArray(receiveList)) {
            for (var j = 0; j < receiveList.length; j++) {
                var entry = receiveList[j]
                var addr = (entry && entry.address) ? entry.address : (typeof entry === "string" ? entry : "")
                if (!addr || seen[addr])
                    continue
                seen[addr] = true
                pushEntry(addr, balanceMap[addr])
            }
        }

        if (Array.isArray(balanceList)) {
            for (var k = 0; k < balanceList.length; k++) {
                var b2 = balanceList[k]
                if (b2 && b2.address && !seen[b2.address]) {
                    seen[b2.address] = true
                    pushEntry(b2.address, b2)
                }
            }
        }

        addressList = merged

        if (balanceTotals && typeof balanceTotals === "object") {
            walletSummary = {
                confirmed: formatQbxAmount(balanceTotals.confirmed),
                unconfirmed: formatQbxAmount(balanceTotals.unconfirmed),
                immature: formatQbxAmount(balanceTotals.immature),
                total: formatQbxAmount(balanceTotals.total)
            }
        } else {
            var sumConfirmed = 0, sumUnconfirmed = 0, sumImmature = 0
            for (var m = 0; m < merged.length; m++) {
                sumConfirmed += merged[m].confirmed
                sumUnconfirmed += merged[m].unconfirmed
                sumImmature += merged[m].immature
            }
            walletSummary = {
                confirmed: sumConfirmed + " QBX",
                unconfirmed: sumUnconfirmed + " QBX",
                immature: sumImmature + " QBX",
                total: (sumConfirmed + sumUnconfirmed + sumImmature) + " QBX"
            }
        }
    }

    function tryApplyMergedAddresses() {
        if (!_recvDone || !_balDone)
            return
        if (_loadWalletTag !== selectedWallet)
            return
        var balanceList = []
        var balanceTotals = null
        if (_balListResult && typeof _balListResult === "object") {
            if (_balListResult.by_address !== undefined)
                balanceList = (_balListResult.by_address instanceof Array) ? _balListResult.by_address : []
            if (_balListResult.totals !== undefined)
                balanceTotals = _balListResult.totals
        }
        var receiveList = (_recvListResult instanceof Array) ? _recvListResult : []
        mergeAndApplyAddresses(receiveList, balanceList, balanceTotals)
        if (errorLabel)
            errorLabel.text = ""
        isLoading = false
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
        resetLoadState(w)

        // All receive addresses (including zero-balance / never funded) from wallet address book
        cliBridge.callNamedWithTag("listreceivedbyaddress", {
            "minconf": 0,
            "include_empty": true
        }, w, "recv:" + w)

        // minconf=1: depth>=1 -> confirmed, depth 0 (mempool) -> unconfirmed.
        // minconf=0 incorrectly classifies mempool outputs as confirmed.
        cliBridge.callNamedWithTag("getaddressbalances", {
            "minconf": 1,
            "include_unsafe": true
        }, w, "bal:" + w)
    }

    Connections {
        target: cliBridge
        function onSuccessWithTag(result, tag) {
            if (selectedWallet === "")
                return
            if (tag === "recv:" + selectedWallet) {
                _recvListResult = (result instanceof Array) ? result : []
                _recvDone = true
                tryApplyMergedAddresses()
                return
            }
            if (tag === "bal:" + selectedWallet) {
                _balListResult = result
                _balDone = true
                tryApplyMergedAddresses()
                return
            }
        }
        function onErrorOccurredWithTag(errorMessage, tag) {
            if (selectedWallet === "")
                return
            if (tag === "recv:" + selectedWallet) {
                _recvListResult = []
                _recvDone = true
                tryApplyMergedAddresses()
                return
            }
            if (tag === "bal:" + selectedWallet) {
                _balListResult = { by_address: [] }
                _balDone = true
                if (errorLabel)
                    errorLabel.text = errorMessage
                tryApplyMergedAddresses()
                return
            }
        }
        function onSuccess(result) {
            // getnewaddress (Generate PQ Address dialog) still uses untagged call
            if (typeof result === "string" || (result && result.address)) {
                var addr = (typeof result === "string") ? result : result.address
                lastGeneratedAddress = addr
                lastStatusMessage = "Generated address for wallet " + generateDialogChosenWallet + ": " + addr
                if (errorLabel)
                    errorLabel.text = ""
                if (generateDialogChosenWallet === selectedWallet) {
                    insertAddressImmediate(addr)
                    loadAddresses(selectedWallet)
                } else {
                    isLoading = false
                }
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
            Qt.callLater(function() {
                if (walletManager && walletManager.wallets && walletManager.wallets.length > 0) {
                    if (selectedWallet !== "") {
                        var idx = walletManager.wallets.indexOf(selectedWallet)
                        if (idx >= 0 && walletCombo.currentIndex !== idx)
                            walletCombo.currentIndex = idx
                    } else if (walletCombo.currentIndex < 0) {
                        walletCombo.currentIndex = 0
                    }
                }
                syncWalletFromCombo()
            })
        }
        function onWalletsChanged() {
            Qt.callLater(function() {
                if (!walletManager || !walletManager.wallets || walletManager.wallets.length === 0) {
                    selectedWallet = ""
                    addressList = []
                    return
                }
                if (selectedWallet !== "") {
                    var idx = walletManager.wallets.indexOf(selectedWallet)
                    if (idx >= 0) {
                        if (walletCombo.currentIndex !== idx)
                            walletCombo.currentIndex = idx
                    } else if (walletCombo.currentIndex < 0) {
                        walletCombo.currentIndex = 0
                    }
                } else if (walletCombo.currentIndex < 0) {
                    walletCombo.currentIndex = 0
                }
                syncWalletFromCombo()
            })
        }
    }

    Connections {
        target: rpcBootstrap
        function onRpcReadyChanged() {
            if (rpcBootstrap && rpcBootstrap.rpcReady)
                Qt.callLater(refresh)
        }
    }

    Component.onCompleted: {
        if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && walletCombo.currentIndex < 0)
            walletCombo.currentIndex = 0
        Qt.callLater(syncWalletFromCombo)
    }

    onVisibleChanged: {
        if (visible) {
            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && walletCombo.currentIndex < 0)
                walletCombo.currentIndex = 0
            Qt.callLater(syncWalletFromCombo)
        }
    }

    function startGenerateFlow() {
        var list = walletManager ? walletManager.wallets : []
        if (!list || list.length === 0)
            return
        if (list.length === 1) {
            generateDialogChosenWallet = list[0]
            isLoading = true
            if (errorLabel) errorLabel.text = ""
            lastStatusMessage = ""
            cliBridge.call("getnewaddress", ["", "pq"], list[0])
            return
        }
        generateWalletDialog.open()
    }

    QbxPageLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            QbxSectionTitle { text: "Addresses" }
            Item { Layout.fillWidth: true }
            QbxButton {
                text: "Generate PQ Address"
                primary: true
                compact: true
                enabled: !isLoading && settingsManager && settingsManager.qbitxCliPath !== "" && !walletBusy && walletManager && walletManager.wallets && walletManager.wallets.length > 0
                onClicked: startGenerateFlow()
            }
            QbxButton {
                text: "Refresh"
                compact: true
                enabled: !isLoading && selectedWallet !== "" && settingsManager && settingsManager.qbitxCliPath !== "" && !walletBusy
                onClicked: loadAddresses(selectedWallet)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: QbxTheme.controlSpacing
            Layout.preferredHeight: QbxTheme.formRowHeight

            Label {
                text: "Wallet"
                color: QbxTheme.textSecondary
                font.pixelSize: 13
                Layout.preferredWidth: QbxTheme.labelColumnWidth
                Layout.maximumWidth: QbxTheme.labelColumnWidth
                Layout.alignment: Qt.AlignVCenter
            }
            QbxComboBox {
                id: walletCombo
                Layout.fillWidth: true
                Layout.maximumWidth: 360
                Layout.preferredHeight: QbxTheme.formRowHeight
                model: walletManager ? walletManager.wallets : []
                onActivated: {
                    if (walletManager && walletManager.wallets && index >= 0 && index < walletManager.wallets.length) {
                        var w = walletManager.wallets[index]
                        if (w !== selectedWallet) {
                            selectedWallet = w
                            loadAddresses(w)
                        }
                    }
                }
                onCurrentIndexChanged: syncWalletFromCombo()
                Component.onCompleted: {
                    if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && currentIndex < 0)
                        currentIndex = 0
                }
            }
            Rectangle {
                visible: selectedWallet !== ""
                height: 24
                width: Math.min(activeWalletLabel.implicitWidth + 16, 180)
                radius: QbxTheme.radiusSmall
                color: Qt.rgba(QbxTheme.accent.r, QbxTheme.accent.g, QbxTheme.accent.b, 0.12)
                border.color: Qt.rgba(QbxTheme.accent.r, QbxTheme.accent.g, QbxTheme.accent.b, 0.35)
                border.width: 1
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: 180
                clip: true

                Text {
                    id: activeWalletLabel
                    anchors.centerIn: parent
                    text: selectedWallet !== "" ? selectedWallet : ""
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: QbxTheme.accentGlow
                    elide: Text.ElideRight
                }
            }
        }

        StatusPanel {
            message: settingsManager && settingsManager.qbitxCliPath === "" ? "Configure qbitx-cli in Settings" : ""
            panelType: "warning"
        }

        StatusPanel {
            message: walletManager && walletManager.lastError !== "" ? walletManager.lastError : ""
            panelType: "error"
        }

        StatusPanel {
            message: lastStatusMessage
            panelType: "info"
        }

        StatusPanel {
            message: errorLabel.text
            panelType: "error"
        }

        Dialog {
            id: generateWalletDialog
            title: "Generate PQ Address"
            modal: true
            standardButtons: Dialog.NoButton
            width: 340

            background: Rectangle {
                color: QbxTheme.bgCard
                radius: QbxTheme.radiusMedium
                border.color: QbxTheme.border
            }

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
                Label { text: "Select wallet:"; color: QbxTheme.textSecondary; font.pixelSize: 13 }
                ListView {
                    id: generateWalletList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(200, Math.max(120, (walletManager ? walletManager.wallets : []).length * 44))
                    clip: true
                    model: walletManager ? walletManager.wallets : []
                    currentIndex: -1
                    delegate: ItemDelegate {
                        width: generateWalletList.width
                        height: 40
                        text: modelData
                        highlighted: generateWalletList.currentIndex === index
                        onClicked: generateWalletList.currentIndex = index
                        contentItem: Text {
                            text: parent.text
                            color: QbxTheme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.highlighted ? QbxTheme.bgActive : (parent.hovered ? QbxTheme.bgHover : "transparent")
                        }
                    }
                }
            }

            footer: RowLayout {
                Item { Layout.fillWidth: true }
                QbxButton { text: "Cancel"; compact: true; onClicked: generateWalletDialog.reject() }
                QbxButton {
                    text: "Generate"
                    primary: true
                    compact: true
                    enabled: generateWalletList.currentIndex >= 0
                    onClicked: generateWalletDialog.accept()
                }
            }
        }

        RowLayout {
            visible: isLoading
            spacing: 8
            BusyIndicator { running: isLoading; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Label { text: "Loading…"; color: QbxTheme.textMuted; font.pixelSize: 13 }
        }

        QbxCard {
            Layout.fillWidth: true
            title: "Address balances"

            ListView {
                id: addressListView
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                Layout.minimumHeight: 200
                model: addressList
                clip: true
                spacing: 1

                header: Rectangle {
                    width: addressListView.width
                    height: 34
                    color: QbxTheme.bgActive
                    border.color: QbxTheme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        Text { Layout.fillWidth: true; text: "Address"; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary }
                        Text { Layout.preferredWidth: 72; text: "Confirmed"; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 72; text: "Unconf."; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 72; text: "Immature"; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 48; text: "UTXOs"; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 72; text: "Total"; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 52; text: "Copy"; font.bold: true; font.pixelSize: 11; color: QbxTheme.textSecondary; horizontalAlignment: Text.AlignHCenter }
                    }
                }

                delegate: Rectangle {
                    width: addressListView.width
                    height: 44
                    color: {
                        if (modelData && modelData.address && lastGeneratedAddress && modelData.address === lastGeneratedAddress)
                            return Qt.rgba(QbxTheme.success.r, QbxTheme.success.g, QbxTheme.success.b, 0.12)
                        return index % 2 === 0 ? QbxTheme.bgInput : Qt.darker(QbxTheme.bgInput, 1.04)
                    }
                    border.color: QbxTheme.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: (modelData && modelData.address) ? modelData.address : "N/A"
                            font.pixelSize: 11
                            font.family: "Consolas,Courier New,monospace"
                            color: QbxTheme.textPrimary
                            elide: Text.ElideMiddle
                        }
                        Text { Layout.preferredWidth: 72; text: formatAmountNumber(modelData && modelData.confirmed); font.pixelSize: 11; color: QbxTheme.textPrimary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 72; text: formatAmountNumber(modelData && modelData.unconfirmed); font.pixelSize: 11; color: QbxTheme.textPrimary; horizontalAlignment: Text.AlignRight }
                        Text { Layout.preferredWidth: 72; text: formatAmountNumber(modelData && modelData.immature); font.pixelSize: 11; color: QbxTheme.textPrimary; horizontalAlignment: Text.AlignRight }
                        Text {
                            Layout.preferredWidth: 48
                            text: (modelData && (modelData.utxos !== undefined || modelData.utxo_count !== undefined))
                                  ? (modelData.utxos || modelData.utxo_count || 0) : 0
                            font.pixelSize: 11
                            color: QbxTheme.textPrimary
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            Layout.preferredWidth: 72
                            text: formatAmountNumber(modelData ? ((modelData.confirmed || 0) + (modelData.unconfirmed || 0) + (modelData.immature || 0)) : 0)
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: QbxTheme.accentGlow
                            horizontalAlignment: Text.AlignRight
                        }
                        QbxButton {
                            Layout.preferredWidth: 52
                            text: "Copy"
                            compact: true
                            enabled: !!(modelData && modelData.address)
                            onClicked: {
                                if (modelData && modelData.address) {
                                    copyHelper.text = modelData.address
                                    copyHelper.selectAll()
                                    copyHelper.copy()
                                    lastStatusMessage = "Address copied to clipboard"
                                }
                            }
                        }
                    }
                }
            }
        }

        QbxCard {
            Layout.fillWidth: true
            compact: true
            title: "Wallet totals"

            QbxInfoRow { label: "Confirmed"; value: formatQbxAmount(walletSummary.confirmed); highlight: true }
            QbxInfoRow { label: "Unconfirmed"; value: formatQbxAmount(walletSummary.unconfirmed) }
            QbxInfoRow { label: "Immature"; value: formatQbxAmount(walletSummary.immature) }
            QbxInfoRow { label: "Total"; value: formatQbxAmount(walletSummary.total); highlight: true }
        }

        TextField {
            id: copyHelper
            visible: false
        }
    }

    Text {
        id: errorLabel
        visible: false
    }
}
