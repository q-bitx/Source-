import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QBitX 1.0
import "../components"

Page {
    id: walletsPage

    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string statusMessage: {
        if (walletManager.lastError !== "") return walletManager.lastError
        if (walletManager.lastInfo !== "") return walletManager.lastInfo
        if (!hasWallet && (!walletManager.loadedWallets || walletManager.loadedWallets.length === 0))
            return "No wallet loaded. Create or restore a wallet, then load it."
        return ""
    }
    property string statusType: walletManager.lastError !== "" ? "error" : "info"

    function refresh() {
        if (walletManager && settingsManager && settingsManager.qbitxCliPath !== "")
            walletManager.refreshWallets()
    }

    Component.onCompleted: refresh()

    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(720, parent ? parent.width - 48 : 720)
        spacing: 20

        Text {
            text: "Wallets"
            font.pixelSize: 22
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 720
        }

        RowLayout {
            spacing: 16
            Layout.alignment: Qt.AlignLeft
            Layout.maximumWidth: 720

            Button {
                text: "Create"
                Layout.minimumWidth: 120
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: createWalletDialog.open()
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
                text: "Load"
                Layout.minimumWidth: 120
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: loadWalletDialog.open()
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
                text: "Restore"
                Layout.minimumWidth: 120
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: restoreWalletDialog.open()
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
                text: "Backup"
                Layout.minimumWidth: 120
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: walletManager && !walletManager.walletBusy && hasWallet
                onClicked: backupWalletDialog.open()
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
                Layout.minimumWidth: 120
                Layout.preferredHeight: 52
                font.pixelSize: 17
                font.weight: Font.DemiBold
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: walletManager.refreshWallets()
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
        }

        StatusPanel {
            message: settingsManager && settingsManager.qbitxCliPath === "" ? "Configure qbitx-cli in Settings" : ""
            panelType: "warning"
            Layout.fillWidth: true
            Layout.maximumWidth: 720
        }

        StatusPanel {
            message: statusMessage
            panelType: statusType
            Layout.fillWidth: true
            Layout.maximumWidth: 720
        }

        Text {
            text: "Loaded Wallets"
            font.pixelSize: 18
            font.bold: true
            Layout.maximumWidth: 720
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            spacing: 8

            Repeater {
                model: walletManager && walletManager.wallets ? walletManager.wallets : []
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: walletNameLabel.implicitHeight + 20
                    Layout.maximumWidth: 720
                    color: "#f0f0f0"
                    radius: 8
                    border.color: "#e0e0e0"
                    border.width: 1

                    Text {
                        id: walletNameLabel
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        text: modelData
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                text: "No wallets loaded."
                font.pixelSize: 14
                color: "#666"
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                visible: !hasWallet
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignCenter
            running: walletManager ? walletManager.walletBusy : false
        }
    }

    Dialog {
        id: createWalletDialog
        title: "Create Wallet"
        width: 420
        height: 200

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text { text: "Wallet name:"; font.pixelSize: 14 }
            TextField {
                id: newWalletName
                Layout.fillWidth: true
                placeholderText: "e.g. pqwallet"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Create"
                    Layout.fillWidth: true
                    enabled: !walletManager.walletBusy && newWalletName.text.trim() !== ""
                    onClicked: {
                        var name = newWalletName.text.trim()
                        if (name) {
                            walletManager.createWallet(name)
                            createWalletDialog.close()
                            newWalletName.text = ""
                        }
                    }
                }

                Button {
                    text: "Cancel"
                    Layout.fillWidth: true
                    onClicked: createWalletDialog.close()
                }
            }
        }
    }

    Dialog {
        id: loadWalletDialog
        title: "Load Wallet"
        width: 420
        height: 200

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text { text: "Wallet name to load (must exist in wallet dir):"; font.pixelSize: 14 }
            TextField {
                id: loadWalletNameField
                Layout.fillWidth: true
                placeholderText: "e.g. 111 or pqwallet"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Load"
                    Layout.fillWidth: true
                    enabled: !walletManager.walletBusy && loadWalletNameField.text.trim() !== ""
                    onClicked: {
                        var name = loadWalletNameField.text.trim()
                        if (name) {
                            walletManager.loadWallet(name)
                            loadWalletDialog.close()
                            loadWalletNameField.text = ""
                        }
                    }
                }

                Button {
                    text: "Cancel"
                    Layout.fillWidth: true
                    onClicked: loadWalletDialog.close()
                }
            }
        }
    }

    Dialog {
        id: backupWalletDialog
        title: "Backup Wallet"
        modal: true
        width: 360
        standardButtons: Dialog.NoButton
        onOpened: {
            var list = walletManager ? walletManager.wallets : []
            if (list.length > 0 && backupWalletList.currentIndex < 0)
                backupWalletList.currentIndex = 0
        }
        onAccepted: {
            var list = walletManager ? walletManager.wallets : []
            if (backupWalletList.currentIndex >= 0 && backupWalletList.currentIndex < list.length) {
                var w = list[backupWalletList.currentIndex]
                if (w)
                    walletManager.backupWallet(w)
            }
        }
        contentItem: ColumnLayout {
            Label { text: "Select wallet to backup:"; font.pixelSize: 14 }
            ListView {
                id: backupWalletList
                Layout.preferredWidth: 320
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
                    width: backupWalletList.width - 4
                    height: 40
                    text: modelData
                    font.pixelSize: 14
                    highlighted: backupWalletList.currentIndex === index
                    onClicked: backupWalletList.currentIndex = index
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
                onClicked: backupWalletDialog.reject()
            }
            Button {
                text: "OK"
                enabled: backupWalletList.currentIndex >= 0 && walletManager && walletManager.wallets && backupWalletList.currentIndex < walletManager.wallets.length
                onClicked: backupWalletDialog.accept()
            }
        }
    }

    Dialog {
        id: restoreWalletDialog
        title: "Restore Wallet"
        modal: true
        width: 720
        closePolicy: Dialog.CloseOnEscape | Dialog.NoAutoClose

        ColumnLayout {
            width: parent.width - 40
            spacing: 16

            Text {
                text: "Path to folder that contains wallet data (wallet name = folder basename)"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: restoreFolderPath
                Layout.fillWidth: true
                placeholderText: "/home/user/backups/backup_YYYYMMDD_HHMMSS/111"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                layoutDirection: Qt.RightToLeft

                Button {
                    text: "Cancel"
                    onClicked: restoreWalletDialog.close()
                }
                Button {
                    text: "Restore"
                    highlighted: true
                    enabled: !walletManager.walletBusy && restoreFolderPath.text.trim() !== ""
                    onClicked: {
                        var path = restoreFolderPath.text.trim()
                        if (path) {
                            walletManager.restoreWalletFromFolder(path)
                            restoreWalletDialog.close()
                            restoreFolderPath.text = ""
                        }
                    }
                }
            }
        }
    }
}
