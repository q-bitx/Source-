import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0
import "../theme" 1.0

Page {
    id: sendPage
    background: Rectangle { color: "transparent" }

    property var fundedFromAddresses: []
    property bool isLoadingBalances: false
    property bool isSending: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string selectedWallet: ""
    property string resultLog: ""
    property string lastCmdLine: ""
    property bool pendingSend: false
    property string pendingSendTag: ""
    property string fromBalanceText: ""
    property bool fromBalanceError: false

    readonly property var amountFormatRe: /^\s*(?:0|[1-9]\d*)(?:[.,]\d{0,8})?\s*$/
    // Dual-RPC load state (same merge approach as Addresses page)
    property var _recvListResult: null
    property var _balListResult: null
    property bool _recvDone: false
    property bool _balDone: false
    property string _loadWalletTag: ""

    readonly property var feeModeValues: ["low", "normal", "high"]
    readonly property bool isLoading: isSending

    function formatQbx(n) {
        if (n === undefined || n === null || isNaN(n)) return "0"
        var num = Number(n)
        if (num === 0) return "0"
        var s = num.toFixed(8)
        s = s.replace(/\.?0+$/, "")
        return s
    }

    function addressTotal(b) {
        if (!b) return 0
        var c = (b.confirmed !== undefined) ? Number(b.confirmed) : 0
        var u = (b.unconfirmed !== undefined) ? Number(b.unconfirmed) : 0
        var im = (b.immature !== undefined) ? Number(b.immature) : 0
        if (isNaN(c)) c = 0
        if (isNaN(u)) u = 0
        if (isNaN(im)) im = 0
        return c + u + im
    }

    function shortAddress(addr) {
        if (!addr || typeof addr !== "string")
            return ""
        if (addr.length <= 20)
            return addr
        return addr.slice(0, 10) + "…" + addr.slice(-8)
    }

    function buildFromAddressLabel(entry) {
        if (!entry || !entry.address)
            return ""
        return shortAddress(entry.address) + " — " + formatQbx(addressTotal(entry)) + " QBX"
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

    function resetLoadState(walletTag) {
        _recvListResult = null
        _balListResult = null
        _recvDone = false
        _balDone = false
        _loadWalletTag = walletTag
    }

    function mergeAddressEntries(receiveList, balanceList) {
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
                immature: parsed.immature
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

        return merged
    }

    function applyFundedFromAddresses(mergedList) {
        var funded = []
        for (var i = 0; i < mergedList.length; i++) {
            var e = mergedList[i]
            if (e && e.address && addressTotal(e) > 0) {
                funded.push({
                    address: e.address,
                    confirmed: e.confirmed,
                    unconfirmed: e.unconfirmed,
                    immature: e.immature,
                    label: buildFromAddressLabel(e)
                })
            }
        }
        fundedFromAddresses = funded
        fromBalanceError = false
        selectDefaultFromAddress()
        updateFromBalanceText()
    }

    function tryApplyMergedAddresses() {
        if (!_recvDone || !_balDone)
            return
        if (_loadWalletTag !== selectedWallet)
            return

        var balanceList = []
        if (_balListResult && typeof _balListResult === "object" && _balListResult.by_address !== undefined)
            balanceList = (_balListResult.by_address instanceof Array) ? _balListResult.by_address : []

        var receiveList = (_recvListResult instanceof Array) ? _recvListResult : []
        applyFundedFromAddresses(mergeAddressEntries(receiveList, balanceList))
        isLoadingBalances = false
    }

    function selectDefaultFromAddress() {
        Qt.callLater(function() {
            if (fundedFromAddresses.length === 0) {
                fromAddressCombo.currentIndex = -1
                return
            }
            if (fundedFromAddresses.length === 1) {
                fromAddressCombo.currentIndex = 0
                updateFromBalanceText()
                return
            }
            var bestIdx = 0
            var bestTotal = addressTotal(fundedFromAddresses[0])
            for (var i = 1; i < fundedFromAddresses.length; i++) {
                var t = addressTotal(fundedFromAddresses[i])
                if (t > bestTotal) {
                    bestTotal = t
                    bestIdx = i
                }
            }
            fromAddressCombo.currentIndex = bestIdx
            updateFromBalanceText()
        })
    }

    function updateFromBalanceText() {
        if (fromBalanceError) {
            fromBalanceText = "Balance unavailable"
            return
        }
        var addr = getFromAddress()
        if (!addr || addr === "") {
            fromBalanceText = fundedFromAddresses.length === 0 && selectedWallet !== "" && !isLoadingBalances
                    ? "No funded addresses found for this wallet."
                    : "Select a from address"
            return
        }
        for (var i = 0; i < fundedFromAddresses.length; i++) {
            var b = fundedFromAddresses[i]
            if (b && b.address === addr) {
                var c = (b.confirmed !== undefined) ? b.confirmed : 0
                var u = (b.unconfirmed !== undefined) ? b.unconfirmed : 0
                var im = (b.immature !== undefined) ? b.immature : 0
                var t = c + u + im
                fromBalanceText = "Confirmed " + formatQbx(c) + " · Unconfirmed " + formatQbx(u) + " · Total " + formatQbx(t) + " QBX"
                return
            }
        }
        fromBalanceText = "Address not found in wallet"
    }

    function loadAddressBalances() {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        if (selectedWallet === "") {
            fundedFromAddresses = []
            updateFromBalanceText()
            return
        }
        if (walletBusy)
            return

        isLoadingBalances = true
        fromBalanceError = false
        fundedFromAddresses = []
        resetLoadState(selectedWallet)

        cliBridge.callNamedWithTag("listreceivedbyaddress", {
            "minconf": 0,
            "include_empty": true
        }, selectedWallet, "sendrecv:" + selectedWallet)

        cliBridge.callNamedWithTag("getaddressbalances", {
            "minconf": 1,
            "include_unsafe": true
        }, selectedWallet, "sendbal:" + selectedWallet)
    }

    function getFromAddress() {
        if (fromAddressCombo.currentIndex >= 0
                && fromAddressCombo.currentIndex < fundedFromAddresses.length)
            return fundedFromAddresses[fromAddressCombo.currentIndex].address
        return ""
    }

    function getFeeMode() {
        var i = feeModeCombo.currentIndex
        return (i >= 0 && i < feeModeValues.length) ? feeModeValues[i] : "normal"
    }

    function normalizedAmountText() {
        return amountField.text ? amountField.text.trim().replace(",", ".") : ""
    }

    function parsedAmount() {
        var t = normalizedAmountText()
        if (t === "")
            return NaN
        return parseFloat(t)
    }

    function getSelectedFromBalance() {
        var addr = getFromAddress()
        for (var i = 0; i < fundedFromAddresses.length; i++) {
            var b = fundedFromAddresses[i]
            if (b && b.address === addr)
                return addressTotal(b)
        }
        return NaN
    }

    function amountFormatLooksValid(text) {
        var t = text ? text.trim() : ""
        if (t === "")
            return false
        if (!amountFormatRe.test(t))
            return false

        var dotCount = (t.match(/\./g) || []).length
        var commaCount = (t.match(/,/g) || []).length
        if (dotCount + commaCount > 1)
            return false

        var norm = t.replace(",", ".")
        if (norm.indexOf(".") < 0 && /^0\d+/.test(norm))
            return false
        if (/^0+\d/.test(norm) && norm.charAt(1) !== ".")
            return false

        return true
    }

    function isValidAmount() {
        if (!amountFormatLooksValid(amountField.text))
            return false
        var n = parsedAmount()
        if (isNaN(n) || n <= 0)
            return false

        var norm = normalizedAmountText()
        var dotIdx = norm.indexOf(".")
        if (dotIdx >= 0 && norm.length - dotIdx - 1 > 8)
            return false

        var bal = getSelectedFromBalance()
        if (!isNaN(bal) && n > bal + 1e-12)
            return false

        return true
    }

    function amountErrorText() {
        var t = amountField.text ? amountField.text.trim() : ""
        if (t === "")
            return ""

        if (!amountFormatLooksValid(t))
            return "Enter a valid amount, for example 0.001"

        var n = parsedAmount()
        if (isNaN(n) || n <= 0)
            return "Enter a valid amount, for example 0.001"

        var norm = normalizedAmountText()
        var dotIdx = norm.indexOf(".")
        if (dotIdx >= 0 && norm.length - dotIdx - 1 > 8)
            return "Enter a valid amount, for example 0.001"

        var bal = getSelectedFromBalance()
        if (!isNaN(bal) && n > bal + 1e-12)
            return "Amount exceeds selected address balance"

        return ""
    }

    function simplifyCliError(errorMessage) {
        if (!errorMessage || errorMessage === "")
            return "Transaction failed."
        var lines = errorMessage.split("\n")
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i].trim()
            if (line === "")
                continue
            if (line.toLowerCase().indexOf("invalid amount") >= 0)
                return "Invalid amount"
            if (line.toLowerCase().indexOf("error message:") >= 0 && i + 1 < lines.length) {
                var next = lines[i + 1].trim()
                if (next !== "")
                    return next
            }
        }
        return lines[0] ? lines[0].trim() : errorMessage
    }

    function resetSendingState() {
        pendingSend = false
        pendingSendTag = ""
        isSending = false
    }

    function sendTransaction() {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        var fromAddr = getFromAddress()
        var toAddr = toAddressField.text ? toAddressField.text.trim() : ""
        var feeMode = getFeeMode()
        if (fromAddr === "") {
            resultLog = "ERROR\nFrom address is required."
            return
        }
        if (toAddr === "") {
            resultLog = "ERROR\nTo address is required."
            return
        }
        var amountErr = amountErrorText()
        if (amountErr !== "" || !isValidAmount()) {
            resultLog = "ERROR\n" + (amountErr !== "" ? amountErr : "Enter a valid amount, for example 0.001")
            resetSendingState()
            return
        }
        if (selectedWallet === "") {
            resultLog = "ERROR\nSelect a wallet."
            return
        }

        var normalizedAmount = normalizedAmountText()
        lastCmdLine = "qbitx-cli -rpcwallet=" + selectedWallet + " pqsendtoaddress \"" + fromAddr + "\" \"" + toAddr + "\" " + normalizedAmount + " " + feeMode
        resultLog = "Sending transaction…"
        pendingSendTag = "pqsend:" + selectedWallet
        pendingSend = true
        isSending = true
        cliBridge.callWithTag("pqsendtoaddress", [fromAddr, toAddr, normalizedAmount, feeMode], selectedWallet, pendingSendTag)
    }

    function syncWalletFromCombo() {
        if (sendWalletCombo.currentIndex >= 0
                && walletManager
                && walletManager.wallets
                && sendWalletCombo.currentIndex < walletManager.wallets.length) {
            var w = walletManager.wallets[sendWalletCombo.currentIndex]
            if (w !== selectedWallet) {
                selectedWallet = w
                loadAddressBalances()
            } else if (fundedFromAddresses.length === 0 && !isLoadingBalances) {
                loadAddressBalances()
            }
        } else if (sendWalletCombo.currentIndex < 0) {
            selectedWallet = ""
            fundedFromAddresses = []
            updateFromBalanceText()
        }
    }

    function refresh() {
        if (selectedWallet !== "")
            loadAddressBalances()
        else if (walletManager && walletManager.wallets && walletManager.wallets.length > 0)
            sendWalletCombo.currentIndex = 0
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            if (!(pendingSend || isSending))
                return
            resetSendingState()
            var txid = ""
            if (typeof result === "string")
                txid = result
            else if (result && typeof result === "object" && result.txid !== undefined)
                txid = result.txid
            else
                txid = (result && result.txid) ? result.txid : JSON.stringify(result)
            resultLog = "SUCCESS\nTXID:\n" + txid
        }
        function onSuccessWithTag(result, tag) {
            if (tag === pendingSendTag && (pendingSend || isSending)) {
                resetSendingState()
                var txid = ""
                if (typeof result === "string")
                    txid = result
                else if (result && typeof result === "object" && result.txid !== undefined)
                    txid = result.txid
                else
                    txid = (result && result.txid) ? result.txid : JSON.stringify(result)
                resultLog = "SUCCESS\nTXID:\n" + txid
                return
            }
            if (selectedWallet === "")
                return
            if (tag === "sendrecv:" + selectedWallet) {
                _recvListResult = (result instanceof Array) ? result : []
                _recvDone = true
                tryApplyMergedAddresses()
                return
            }
            if (tag === "sendbal:" + selectedWallet) {
                _balListResult = result
                _balDone = true
                tryApplyMergedAddresses()
                return
            }
        }
        function onErrorOccurred(errorMessage) {
            if (!(pendingSend || isSending))
                return
            resetSendingState()
            resultLog = "ERROR\n" + simplifyCliError(errorMessage)
        }
        function onErrorOccurredWithTag(errorMessage, tag) {
            if (tag === pendingSendTag && (pendingSend || isSending)) {
                resetSendingState()
                resultLog = "ERROR\n" + simplifyCliError(errorMessage)
                return
            }
            if (selectedWallet === "")
                return
            if (tag === "sendrecv:" + selectedWallet) {
                _recvListResult = []
                _recvDone = true
                tryApplyMergedAddresses()
                return
            }
            if (tag === "sendbal:" + selectedWallet) {
                _balListResult = { by_address: [] }
                _balDone = true
                fromBalanceError = true
                resultLog = "ERROR (balance load): " + errorMessage
                tryApplyMergedAddresses()
                updateFromBalanceText()
            }
        }
    }

    Connections {
        target: walletManager
        function onLoadedWalletsChanged() {
            Qt.callLater(function() {
                if (walletManager && walletManager.wallets && walletManager.wallets.length > 0) {
                    if (selectedWallet !== "") {
                        var idx = walletManager.wallets.indexOf(selectedWallet)
                        if (idx >= 0 && sendWalletCombo.currentIndex !== idx)
                            sendWalletCombo.currentIndex = idx
                    } else if (sendWalletCombo.currentIndex < 0) {
                        sendWalletCombo.currentIndex = 0
                    }
                }
                syncWalletFromCombo()
            })
        }
        function onWalletsChanged() {
            Qt.callLater(function() {
                if (!walletManager || !walletManager.wallets || walletManager.wallets.length === 0) {
                    selectedWallet = ""
                    fundedFromAddresses = []
                    updateFromBalanceText()
                    return
                }
                if (selectedWallet !== "") {
                    var idx = walletManager.wallets.indexOf(selectedWallet)
                    if (idx >= 0) {
                        if (sendWalletCombo.currentIndex !== idx)
                            sendWalletCombo.currentIndex = idx
                    } else if (sendWalletCombo.currentIndex < 0) {
                        sendWalletCombo.currentIndex = 0
                    }
                } else if (sendWalletCombo.currentIndex < 0) {
                    sendWalletCombo.currentIndex = 0
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
        if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && sendWalletCombo.currentIndex < 0)
            sendWalletCombo.currentIndex = 0
        Qt.callLater(syncWalletFromCombo)
    }

    onVisibleChanged: {
        if (visible) {
            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && sendWalletCombo.currentIndex < 0)
                sendWalletCombo.currentIndex = 0
            Qt.callLater(syncWalletFromCombo)
        }
    }

    QbxPageLayout {
        anchors.fill: parent

        QbxSectionTitle { text: "Send" }

        StatusPanel {
            message: settingsManager && settingsManager.qbitxCliPath === "" ? "Configure qbitx-cli in Settings" : ""
            panelType: "warning"
        }

        StatusPanel {
            message: selectedWallet !== "" && !isLoadingBalances && fundedFromAddresses.length === 0 && !fromBalanceError
                    ? "No funded addresses found for this wallet."
                    : ""
            panelType: "warning"
        }

        StatusPanel {
            message: fromBalanceError ? resultLog : ""
            panelType: "error"
        }

        StatusPanel {
            message: amountField.text.trim() !== "" ? amountErrorText() : ""
            panelType: "error"
        }

        QbxCard {
            Layout.fillWidth: true
            title: "Transaction"

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Layout.preferredHeight: QbxTheme.formRowHeight
                    Label {
                        text: "Wallet"
                        color: QbxTheme.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: QbxTheme.labelColumnWidth
                        Layout.alignment: Qt.AlignVCenter
                    }
                    QbxComboBox {
                        id: sendWalletCombo
                        Layout.fillWidth: true
                        Layout.maximumWidth: 360
                        Layout.preferredHeight: QbxTheme.formRowHeight
                        model: walletManager ? walletManager.wallets : []
                        onActivated: {
                            if (walletManager && walletManager.wallets && index >= 0 && index < walletManager.wallets.length) {
                                var w = walletManager.wallets[index]
                                if (w !== selectedWallet) {
                                    selectedWallet = w
                                    loadAddressBalances()
                                }
                            }
                        }
                        onCurrentIndexChanged: syncWalletFromCombo()
                        Component.onCompleted: {
                            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && currentIndex < 0)
                                currentIndex = 0
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Layout.preferredHeight: QbxTheme.formRowHeight
                    Label {
                        text: "From address"
                        color: QbxTheme.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: QbxTheme.labelColumnWidth
                        Layout.alignment: Qt.AlignVCenter
                    }
                    QbxComboBox {
                        id: fromAddressCombo
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        Layout.preferredHeight: QbxTheme.formRowHeight
                        enabled: fundedFromAddresses.length > 0 && !isLoadingBalances
                        model: fundedFromAddresses
                        textRole: "label"
                        onCurrentIndexChanged: updateFromBalanceText()
                    }
                    BusyIndicator {
                        visible: isLoadingBalances
                        running: isLoadingBalances
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: QbxTheme.labelColumnWidth + 10
                    text: fromBalanceText
                    font.pixelSize: 12
                    color: fromBalanceError ? QbxTheme.error : QbxTheme.textMuted
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Layout.preferredHeight: QbxTheme.formRowHeight
                    Label {
                        text: "To address"
                        color: QbxTheme.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: QbxTheme.labelColumnWidth
                        Layout.alignment: Qt.AlignVCenter
                    }
                    QbxTextField {
                        id: toAddressField
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        Layout.preferredHeight: QbxTheme.formRowHeight
                        placeholderText: "Destination PQ address"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Layout.preferredHeight: QbxTheme.formRowHeight
                    Label {
                        text: "Amount (QBX)"
                        color: QbxTheme.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: QbxTheme.labelColumnWidth
                        Layout.alignment: Qt.AlignVCenter
                    }
                    QbxTextField {
                        id: amountField
                        Layout.preferredWidth: 220
                        Layout.maximumWidth: 220
                        Layout.preferredHeight: QbxTheme.formRowHeight
                        placeholderText: "0.001"
                        validator: RegularExpressionValidator {
                            regularExpression: /^\d*[.,]?\d{0,8}$/
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Layout.preferredHeight: QbxTheme.formRowHeight
                    Label {
                        text: "Fee mode"
                        color: QbxTheme.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: QbxTheme.labelColumnWidth
                        Layout.alignment: Qt.AlignVCenter
                    }
                    QbxComboBox {
                        id: feeModeCombo
                        Layout.preferredWidth: 180
                        Layout.maximumWidth: 180
                        Layout.preferredHeight: QbxTheme.formRowHeight
                        model: ["Low", "Normal", "High"]
                        currentIndex: 1
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 10
                    Item { Layout.preferredWidth: QbxTheme.labelColumnWidth }
                    QbxButton {
                        text: isSending ? "Sending…" : "Send"
                        primary: true
                        enabled: !isSending && !isLoadingBalances && !walletBusy
                                 && selectedWallet !== ""
                                 && getFromAddress() !== ""
                                 && fundedFromAddresses.length > 0
                                 && (toAddressField.text && toAddressField.text.trim() !== "")
                                 && isValidAmount()
                        onClicked: sendTransaction()
                    }
                    BusyIndicator {
                        running: isSending
                        visible: isSending
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }
                }
            }
        }

        QbxCard {
            Layout.fillWidth: true
            compact: true
            title: "Result"

            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                readOnly: true
                text: resultLog
                wrapMode: TextArea.WrapAnywhere
                color: resultLog.indexOf("ERROR") === 0 ? QbxTheme.error : QbxTheme.textPrimary
                font.pixelSize: 12
                font.family: "Consolas,Courier New,monospace"
                background: Rectangle {
                    color: QbxTheme.bgInput
                    radius: QbxTheme.radiusSmall
                    border.color: QbxTheme.border
                }
                placeholderText: "TXID or error will appear here…"
                placeholderTextColor: QbxTheme.textMuted
            }
        }
    }
}
