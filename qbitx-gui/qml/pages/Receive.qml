import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: receivePage

    property string newAddress: ""
    property bool isLoading: false
    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string selectedWallet: ""

    function generateAddress() {
        if (!settingsManager || settingsManager.qbitxCliPath === "")
            return
        if (selectedWallet === "") {
            errorLabel.text = "Select a wallet"
            return
        }
        isLoading = true
        errorLabel.text = ""
        cliBridge.call("getnewaddress", ["", "pq"], selectedWallet)
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            isLoading = false
            if (typeof result === "string") {
                newAddress = result
                // Signal to refresh addresses page if it exists
                // This will be handled by the Addresses page listening to the same signal
            } else if (result.address) {
                newAddress = result.address
            } else {
                newAddress = JSON.stringify(result)
            }
        }
        function onErrorOccurred(errorMessage) {
            isLoading = false
            errorLabel.text = errorMessage
        }
    }

    Connections {
        target: walletManager
        function onLoadedWalletsChanged() {
            if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && receiveWalletCombo.currentIndex < 0)
                receiveWalletCombo.currentIndex = 0
        }
    }

    Component.onCompleted: {
        if (walletManager && walletManager.wallets && walletManager.wallets.length > 0 && receiveWalletCombo.currentIndex < 0)
            receiveWalletCombo.currentIndex = 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        Text {
            text: "Receive"
            font.pixelSize: 24
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: "#fff3cd"
            border.color: "#ffc107"
            border.width: 1
            visible: settingsManager.qbitxCliPath === ""

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
            Label { text: "Wallet:"; font.pixelSize: 14 }
            ComboBox {
                id: receiveWalletCombo
                Layout.preferredWidth: 260
                Layout.preferredHeight: 36
                model: walletManager ? walletManager.wallets : []
                onActivated: {
                    if (walletManager && walletManager.wallets && index >= 0 && index < walletManager.wallets.length)
                        selectedWallet = walletManager.wallets[index]
                }
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && walletManager && walletManager.wallets && currentIndex < walletManager.wallets.length) {
                        var w = walletManager.wallets[currentIndex]
                        if (w !== selectedWallet)
                            selectedWallet = w
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
            id: errorLabel
            color: "red"
            visible: text !== ""
        }

        GroupBox {
            Layout.fillWidth: true
            title: "Generate New Address"

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Button {
                    text: "Generate Address"
                    enabled: !isLoading && selectedWallet !== "" && settingsManager && settingsManager.qbitxCliPath !== ""
                    onClicked: generateAddress()
                }

                BusyIndicator {
                    Layout.alignment: Qt.AlignCenter
                    running: isLoading
                }

                TextField {
                    id: addressField
                    Layout.fillWidth: true
                    text: newAddress
                    readOnly: true
                    placeholderText: "Address will appear here"
                }

                Button {
                    text: "Copy Address"
                    enabled: newAddress !== ""
                    onClicked: {
                        addressField.selectAll()
                        addressField.copy()
                    }
                }
            }
        }
    }
}
