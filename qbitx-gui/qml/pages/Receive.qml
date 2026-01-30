import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: receivePage

    property string newAddress: ""
    property bool isLoading: false

    function generateAddress() {
        if (settingsManager.qbitxCliPath === "") {
            return
        }
        if (settingsManager.activeWallet === "") {
            errorLabel.text = "No active wallet selected"
            return
        }
        isLoading = true
        errorLabel.text = ""
        cliBridge.call("getnewaddress", ["", "pq"], settingsManager.activeWallet)
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

                Text {
                    text: "Active Wallet: " + (settingsManager.activeWallet || "None")
                }

                Button {
                    text: "Generate Address"
                    enabled: !isLoading && settingsManager.activeWallet !== "" && settingsManager.qbitxCliPath !== ""
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
