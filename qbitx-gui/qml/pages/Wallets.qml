import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import QBitX 1.0
import "../components"
import "../theme" 1.0

Page {
    id: walletsPage
    background: Rectangle { color: "transparent" }

    property bool hasWallet: walletManager && walletManager.wallets ? walletManager.wallets.length > 0 : false
    property string pendingBackupWallet: ""
    property string statusMessage: {
        if (walletManager.lastError !== "") return walletManager.lastError
        if (walletManager.lastInfo !== "") return walletManager.lastInfo
        if (!hasWallet && (!walletManager.loadedWallets || walletManager.loadedWallets.length === 0))
            return "No wallet loaded. Create or restore a wallet, then load it."
        return ""
    }
    property string statusType: walletManager.lastError !== "" ? "error" : "info"

    function refresh() {
        if (!visible)
            return
        if (!rpcBootstrap || !rpcBootstrap.rpcReady)
            return
        if (walletManager && settingsManager && settingsManager.qbitxCliPath !== "")
            walletManager.refreshWallets()
    }

    function startBackupFlow() {
        var list = walletManager ? walletManager.wallets : []
        if (!list || list.length === 0)
            return
        if (list.length === 1) {
            openBackupSaveDialog(list[0])
            return
        }
        pendingBackupWallet = ""
        backupWalletPickerDialog.open()
    }

    function openBackupSaveDialog(walletName) {
        pendingBackupWallet = walletName
        var docs = StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        if (docs)
            backupSaveDialog.currentFolder = docs
        backupSaveDialog.open()
    }

    function openRestoreFileDialog() {
        var docs = StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        if (docs)
            restoreOpenDialog.currentFolder = docs
        restoreOpenDialog.open()
    }

    Connections {
        target: rpcBootstrap
        function onRpcReadyChanged() {
            if (visible && rpcBootstrap && rpcBootstrap.rpcReady)
                refresh()
        }
    }

    onVisibleChanged: {
        if (visible && rpcBootstrap && rpcBootstrap.rpcReady)
            refresh()
    }

    QbxPageLayout {
        anchors.fill: parent

        QbxSectionTitle {
            text: "Wallets"
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: QbxTheme.controlSpacing
            Layout.preferredHeight: QbxTheme.actionButtonHeight

            QbxButton {
                text: "Create"
                uniform: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: createWalletDialog.open()
            }
            QbxButton {
                text: "Load"
                uniform: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: loadWalletDialog.open()
            }
            QbxButton {
                text: "Restore"
                uniform: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: openRestoreFileDialog()
            }
            QbxButton {
                text: "Backup"
                primary: true
                uniform: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: walletManager && !walletManager.walletBusy && hasWallet
                onClicked: startBackupFlow()
            }
            QbxButton {
                text: "Refresh"
                uniform: true
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: walletManager && !walletManager.walletBusy && settingsManager && settingsManager.qbitxCliPath !== ""
                onClicked: walletManager.refreshWallets()
            }
        }

        StatusPanel {
            message: settingsManager && settingsManager.qbitxCliPath === "" ? "Configure qbitx-cli in Settings" : ""
            panelType: "warning"
        }

        StatusPanel {
            message: statusMessage
            panelType: statusType === "error" ? "error" : "info"
        }

        QbxCard {
            Layout.fillWidth: true
            compact: true
            title: "Loaded Wallets"

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: hasWallet

                Repeater {
                    model: walletManager && walletManager.wallets ? walletManager.wallets : []
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: QbxTheme.radiusSmall
                        color: Qt.rgba(QbxTheme.accent.r, QbxTheme.accent.g, QbxTheme.accent.b, 0.08)
                        border.color: QbxTheme.border
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10

                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: QbxTheme.success
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: QbxTheme.textPrimary
                                elide: Text.ElideRight
                            }

                            Text {
                                text: walletManager.wallets.length === 1 ? "Active" : "Loaded"
                                font.pixelSize: 11
                                color: QbxTheme.accentGlow
                            }
                        }
                    }
                }
            }

            Text {
                text: "No wallets loaded."
                font.pixelSize: 13
                color: QbxTheme.textMuted
                visible: !hasWallet
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: walletManager ? walletManager.walletBusy : false
        }
    }

    FileDialog {
        id: backupSaveDialog
        title: pendingBackupWallet !== ""
               ? ("Save Wallet Backup — " + walletManager.suggestedBackupFileName(pendingBackupWallet))
               : "Save Wallet Backup"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Wallet backup (*.dat)", "All files (*)"]
        defaultSuffix: "dat"
        onAccepted: {
            if (pendingBackupWallet !== "")
                walletManager.saveWalletBackup(selectedFile, pendingBackupWallet)
        }
    }

    FileDialog {
        id: restoreOpenDialog
        title: "Restore Wallet from Backup"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Wallet backup (*.dat)", "All files (*)"]
        onAccepted: walletManager.restoreWalletFromBackupFile(selectedFile)
    }

    Dialog {
        id: backupWalletPickerDialog
        title: "Select Wallet to Backup"
        modal: true
        width: 380
        standardButtons: Dialog.NoButton
        onOpened: {
            var list = walletManager ? walletManager.wallets : []
            if (list.length > 0 && backupWalletList.currentIndex < 0)
                backupWalletList.currentIndex = 0
        }

        background: Rectangle {
            color: QbxTheme.bgCard
            radius: QbxTheme.radiusMedium
            border.color: QbxTheme.border
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: "Choose wallet:"
                font.pixelSize: 13
                color: QbxTheme.textSecondary
            }
            ListView {
                id: backupWalletList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(200, Math.max(120, (walletManager ? walletManager.wallets : []).length * 44))
                clip: true
                model: walletManager ? walletManager.wallets : []
                currentIndex: -1
                delegate: ItemDelegate {
                    width: backupWalletList.width
                    height: 40
                    text: modelData
                    highlighted: backupWalletList.currentIndex === index
                    onClicked: backupWalletList.currentIndex = index
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
            spacing: 8
            Item { Layout.fillWidth: true }
            QbxButton {
                text: "Cancel"
                compact: true
                onClicked: backupWalletPickerDialog.close()
            }
            QbxButton {
                text: "Continue"
                primary: true
                compact: true
                enabled: backupWalletList.currentIndex >= 0
                onClicked: {
                    var list = walletManager ? walletManager.wallets : []
                    if (backupWalletList.currentIndex >= 0 && backupWalletList.currentIndex < list.length) {
                        backupWalletPickerDialog.close()
                        openBackupSaveDialog(list[backupWalletList.currentIndex])
                    }
                }
            }
        }
    }

    Dialog {
        id: createWalletDialog
        title: "Create Wallet"
        modal: true
        width: 420
        standardButtons: Dialog.NoButton

        background: Rectangle {
            color: QbxTheme.bgCard
            radius: QbxTheme.radiusMedium
            border.color: QbxTheme.border
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label { text: "Wallet name:"; color: QbxTheme.textSecondary; font.pixelSize: 13 }
            QbxTextField {
                id: newWalletName
                Layout.fillWidth: true
                placeholderText: "e.g. pqwallet"
            }
        }

        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            QbxButton {
                text: "Cancel"
                compact: true
                onClicked: createWalletDialog.close()
            }
            QbxButton {
                text: "Create"
                primary: true
                compact: true
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
        }
    }

    Dialog {
        id: loadWalletDialog
        title: "Load Wallet"
        modal: true
        width: 420
        standardButtons: Dialog.NoButton

        background: Rectangle {
            color: QbxTheme.bgCard
            radius: QbxTheme.radiusMedium
            border.color: QbxTheme.border
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: "Wallet name (must exist in wallet directory):"
                color: QbxTheme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            QbxTextField {
                id: loadWalletNameField
                Layout.fillWidth: true
                placeholderText: "e.g. pqwallet"
            }
        }

        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            QbxButton {
                text: "Cancel"
                compact: true
                onClicked: loadWalletDialog.close()
            }
            QbxButton {
                text: "Load"
                primary: true
                compact: true
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
        }
    }
}
