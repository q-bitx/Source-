import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: sendPage

    property var addressBalances: []
    property bool isLoading: false
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string selectedWallet: ""
    property string resultLog: ""
    property string lastCmdLine: ""
    property bool pendingSend: false
    property string fromBalanceText: ""
    property bool fromBalanceError: false
    property int fromBalanceRequestId: 0

    readonly property var feeModeValues: ["low", "normal", "high"]

    function formatQbx(n) {
        if (n === undefined || n === null || isNaN(n)) return "0"
        var num = Number(n)
        if (num === 0) return "0"
        var s = num.toFixed(8)
        s = s.replace(/\.?0+$/, "")
        return s
    }

    function updateFromBalanceText() {
        if (fromBalanceError) {
            fromBalanceText = "From balance: (failed to load)"
            return
        }
        var addr = getFromAddress()
        if (!addr || addr === "") {
            fromBalanceText = "From balance: (select an address)"
            return
        }
        for (var i = 0; i < addressBalances.length; i++) {
            var b = addressBalances[i]
            if (b && b.address === addr) {
                var c = (b.confirmed !== undefined) ? b.confirmed : 0
                var u = (b.unconfirmed !== undefined) ? b.unconfirmed : 0
                var im = (b.immature !== undefined) ? b.immature : 0
                var t = c + u + im
                fromBalanceText = "From balance: Confirmed: " + formatQbx(c) + " QBX | Unconfirmed: " + formatQbx(u) + " QBX | Immature: " + formatQbx(im) + " QBX | Total: " + formatQbx(t) + " QBX"
                return
            }
        }
        fromBalanceText = "From balance: (address not in wallet list)"
    }

    function refreshFromBalance() {
        fromBalanceError = false
        updateFromBalanceText()
    }

    function loadAddressBalances() {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        if (selectedWallet === "")
            return
        if (walletBusy)
            return
        cliBridge.callNamed("getaddressbalances", {
            "minconf": 0,
            "include_unsafe": true
        }, selectedWallet)
    }

    function getFromAddress() {
        var t = fromAddressCombo.currentText ? fromAddressCombo.currentText.trim() : ""
        if (t === "" && fromAddressCombo.editText !== undefined)
            t = fromAddressCombo.editText.trim()
        return t
    }

    function getFeeMode() {
        var i = feeModeCombo.currentIndex
        return (i >= 0 && i < feeModeValues.length) ? feeModeValues[i] : "normal"
    }

    function isAmountValid() {
        var a = amountField.text ? amountField.text.trim() : ""
        if (a === "")
            return false
        var n = parseFloat(a)
        return !isNaN(n) && n > 0
    }

    function sendTransaction() {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        var fromAddr = getFromAddress()
        var toAddr = toAddressField.text ? toAddressField.text.trim() : ""
        var amountStr = amountField.text ? amountField.text.trim() : ""
        var feeMode = getFeeMode()
        if (fromAddr === "") {
            resultLog = "ERROR\nFrom address is required."
            return
        }
        if (toAddr === "") {
            resultLog = "ERROR\nTo address is required."
            return
        }
        if (!isAmountValid()) {
            resultLog = "ERROR\nAmount must be a number greater than 0."
            return
        }
        if (selectedWallet === "") {
            resultLog = "ERROR\nSelect a wallet."
            return
        }

        lastCmdLine = "qbitx-cli -rpcwallet=" + selectedWallet + " pqsendtoaddress \"" + fromAddr + "\" \"" + toAddr + "\" " + amountStr + " " + feeMode
        resultLog = "CMD: " + lastCmdLine
        isLoading = true
        pendingSend = true
        cliBridge.call("pqsendtoaddress", [fromAddr, toAddr, amountStr, feeMode], selectedWallet)
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            if (pendingSend) {
                pendingSend = false
                isLoading = false
                var txid = ""
                if (typeof result === "string")
                    txid = result
                else if (result && typeof result === "object" && result.txid !== undefined)
                    txid = result.txid
                else
                    txid = (result && result.txid) ? result.txid : JSON.stringify(result)
                resultLog = resultLog + "\n\nSUCCESS\nTXID: " + txid
                return
            }
            if (result && typeof result === "object" && result.by_address !== undefined) {
                addressBalances = (result.by_address instanceof Array) ? result.by_address : []
                if (addressBalances.length > 0 && (!fromAddressCombo.currentText || fromAddressCombo.currentText.trim() === ""))
                    fromAddressCombo.currentIndex = 0
                fromBalanceError = false
                updateFromBalanceText()
            }
        }
        function onErrorOccurred(errorMessage) {
            if (pendingSend) {
                pendingSend = false
                isLoading = false
                resultLog = resultLog + "\n\nERROR\n" + errorMessage
            } else {
                fromBalanceError = true
                if (resultLog !== "")
                    resultLog = resultLog + "\n\n"
                resultLog = resultLog + "ERROR (from balance): " + errorMessage
                updateFromBalanceText()
            }
        }
    }

    function refresh() {
        if (selectedWallet !== "")
            loadAddressBalances()
    }

    Connections {
        target: walletManager
        function onLoadedWalletsChanged() {
            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && sendWalletCombo.currentIndex < 0)
                sendWalletCombo.currentIndex = 0
        }
    }

    Component.onCompleted: {
        if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && sendWalletCombo.currentIndex < 0)
            sendWalletCombo.currentIndex = 0
        if (selectedWallet !== "")
            loadAddressBalances()
        refreshFromBalance()
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(720, parent ? parent.width - 48 : 720)
        spacing: 20

        RowLayout {
            spacing: 10
            Layout.alignment: Qt.AlignLeft
            Layout.maximumWidth: 720
            Label { text: "Wallet:"; font.pixelSize: 14 }
            ComboBox {
                id: sendWalletCombo
                Layout.preferredWidth: 220
                Layout.preferredHeight: 36
                model: walletManager ? walletManager.wallets : []
                onActivated: {
                    if (walletManager && walletManager.wallets && index >= 0 && index < walletManager.wallets.length) {
                        selectedWallet = walletManager.wallets[index]
                        loadAddressBalances()
                    }
                }
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && walletManager && walletManager.wallets && currentIndex < walletManager.wallets.length) {
                        var w = walletManager.wallets[currentIndex]
                        if (w !== selectedWallet) {
                            selectedWallet = w
                            loadAddressBalances()
                        }
                        refreshFromBalance()
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
            text: "Send"
            font.pixelSize: 22
            font.bold: true
            Layout.maximumWidth: 720
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
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
            Layout.maximumWidth: 720
            title: "Send Transaction"

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label { text: "From Address"; font.pixelSize: 14 }
                ComboBox {
                    id: fromAddressCombo
                    Layout.fillWidth: true
                    editable: true
                    model: addressBalances
                    textRole: "address"
                    font.pixelSize: 13
                    onCurrentTextChanged: refreshFromBalance()
                    onEditTextChanged: refreshFromBalance()
                }
                Text {
                    Layout.fillWidth: true
                    text: fromBalanceText
                    font.pixelSize: 12
                    color: fromBalanceError ? "#c00" : "#444"
                    wrapMode: Text.WordWrap
                }

                Label { text: "To Address"; font.pixelSize: 14 }
                TextField {
                    id: toAddressField
                    Layout.fillWidth: true
                    placeholderText: "Destination address"
                    font.pixelSize: 13
                }

                Label { text: "Amount (QBX)"; font.pixelSize: 14 }
                TextField {
                    id: amountField
                    Layout.fillWidth: true
                    placeholderText: "0.0"
                    font.pixelSize: 13
                    validator: DoubleValidator { bottom: 0 }
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }

                Label { text: "Fee mode"; font.pixelSize: 14 }
                ComboBox {
                    id: feeModeCombo
                    Layout.fillWidth: true
                    model: ["Low", "Normal", "High"]
                    currentIndex: 1
                    font.pixelSize: 13
                }

                Button {
                    text: "Send"
                    Layout.preferredHeight: 44
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    enabled: !isLoading && !walletBusy && selectedWallet !== "" && getFromAddress() !== "" && (toAddressField.text && toAddressField.text.trim() !== "") && isAmountValid()
                    onClicked: sendTransaction()
                    background: Rectangle {
                        radius: 8
                        border.width: 1
                        border.color: parent.pressed ? "#a0a0a0" : (parent.hovered ? "#999" : "#888")
                        color: parent.enabled ? (parent.pressed ? "#d0d0d0" : (parent.hovered ? "#d5d5d5" : "#c5c5c5")) : "#e8e8e8"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? "#2d2d2d" : "#888"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                BusyIndicator {
                    Layout.alignment: Qt.AlignCenter
                    running: isLoading
                }
            }
        }

        GroupBox {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            Layout.preferredHeight: 160
            title: "Result"

            Item {
                width: parent.width
                height: 160

                TextArea {
                    id: resultArea
                    anchors.fill: parent
                    readOnly: true
                    text: resultLog
                    wrapMode: TextArea.WrapAnywhere
                }

                Text {
                    anchors.left: resultArea.left
                    anchors.leftMargin: 10
                    anchors.top: resultArea.top
                    anchors.topMargin: 8
                    text: "TXID / error will appear here..."
                    opacity: 0.45
                    visible: resultArea.text.length === 0
                    z: 2
                }
            }
        }
    }

}
