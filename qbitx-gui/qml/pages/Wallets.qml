import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QBitX 1.0

Page {
    id: walletsPage

    property var walletList: []
    property bool isLoading: false
    property bool isCreating: false
    property bool isLoadingWallet: false
    property bool isImporting: false
    property bool isBackingUp: false
    property string lastBackupPath: ""
    property string pendingCreatedWallet: ""
    property bool walletBusy: walletManager ? walletManager.walletBusy : false
    property bool hasWallet: settingsManager ? (settingsManager.activeWallet !== "") : false
    
    // Explicit wallet state model to prevent race conditions
    readonly property int walletStateEmpty: 0
    readonly property int walletStateLoading: 1
    readonly property int walletStateLoaded: 2
    readonly property int walletStateError: 3
    
    property int currentWalletState: walletStateEmpty
    property string lastConfirmedActiveWallet: ""  // Last wallet confirmed by listwallets
    property bool preventAutoSelection: false  // Prevent re-triggering auto-selection
    
    // MONOTONIC WALLET STATE GUARD: Once set, activeWallet can only be changed by:
    // 1. Confirmed absence in listwallets response
    // 2. Successful wallet operations (create/load/import)
    property bool walletStateIsMonotonic: false  // Prevents async RPC clearing

    function convertWindowsPathToWSL(path) {
        if (!path || typeof path !== "string") {
            return path
        }
        
        var trimmed = path.trim()
        
        // Check for Windows drive path pattern (e.g., C:\, D:\)
        var windowsDriveRegex = /^([A-Za-z]):[\\\/]/
        var match = trimmed.match(windowsDriveRegex)
        
        if (match) {
            var driveLetter = match[1].toLowerCase()
            var restOfPath = trimmed.substring(3) // Remove "C:\" or "C:/"
            
            // Convert backslashes to forward slashes
            restOfPath = restOfPath.replace(/\\/g, '/')
            
            // Convert to WSL path
            var wslPath = "/mnt/" + driveLetter + "/" + restOfPath
            
            console.log("Converted Windows path:", trimmed, "to WSL path:", wslPath)
            return wslPath
        }
        
        // Not a Windows path, return as-is
        return trimmed
    }

    function refreshWallets() {
        if (!settingsManager || settingsManager.qbitxCliPath === "") {
            return
        }
        if (walletBusy) {
            return
        }
        isLoading = true
        // Call without wallet parameter (global operation)
        cliBridge.call("listwallets", [], "")
    }

    Connections {
        target: cliBridge
        function onSuccess(result) {
            if (result instanceof Array) {
                // LISTWALLETS is the ONLY source of truth for wallet loaded/unloaded state
                isLoading = false
                walletList = result || []
                
                var currentActiveWallet = settingsManager.activeWallet || ""
                var walletStillLoaded = currentActiveWallet !== "" && walletList.indexOf(currentActiveWallet) >= 0
                
                // Handle pending created wallet (explicit user action)
                if (pendingCreatedWallet !== "" && walletList.indexOf(pendingCreatedWallet) >= 0) {
                    settingsManager.activeWallet = pendingCreatedWallet
                    settingsManager.setWalletOrigin(pendingCreatedWallet, "local")
                    pendingCreatedWallet = ""
                    isLoadingWallet = false
                    preventAutoSelection = true
                    console.log("Created wallet set as active:", pendingCreatedWallet)
                }
                // RULE: Auto-select ONLY once at startup when activeWallet is empty
                else if (currentActiveWallet === "" && walletList.length > 0 && !preventAutoSelection) {
                    var firstWallet = String(walletList[0])
                    settingsManager.activeWallet = firstWallet
                    preventAutoSelection = true  // Prevent any re-selection
                    console.log("Startup auto-selection:", firstWallet)
                }
                // RULE: listwallets must NEVER clear activeWallet - keep wallet state stable
                // Removed: wallet clearing logic based on listwallets results
                // Wallet confirmed loaded - keep active (no action needed)
                // RULE: If Core shows wallet in listwallets, GUI must keep it active
            } else if (result.wallet_name) {
                isLoading = false
                var walletNameStr = String(result.wallet_name || "")
                walletList = [walletNameStr]
                // RULE: Only set if no active wallet exists (startup only)
                if (settingsManager.activeWallet === "" && walletNameStr !== "") {
                    settingsManager.activeWallet = walletNameStr
                    preventAutoSelection = true
                }
            } else if (result.name) {
                // createwallet/loadwallet returns {name: "..."}
                var walletName = String(result.name || "")
                var wasCreating = isCreating
                var wasLoadingWallet = isLoadingWallet
                
                if (wasCreating && walletName) {
                    // After createwallet, check if wallet already loaded before calling loadwallet
                    pendingCreatedWallet = walletName
                    isCreating = false
                    
                    // Check if wallet is already in listwallets to prevent unnecessary loadwallet
                    if (walletList.indexOf(walletName) >= 0) {
                        // Wallet already loaded, set as active directly
                        settingsManager.activeWallet = walletName
                        console.log("Wallet already loaded after create:", walletName)
                        refreshWallets() // Refresh to update UI
                        return
                    } else {
                        // Wallet not in list, need to load it
                        isLoadingWallet = true
                        currentWalletState = walletStateLoading
                        cliBridge.call("loadwallet", [walletName], "")
                        return
                    }
                }
                
                // Handle loadwallet success
                if (wasLoadingWallet && walletName) {
                    // Regular wallet load completed
                    settingsManager.activeWallet = walletName
                    console.log("Wallet loaded and set as active:", walletName)
                    
                    // Clean up import process if it was running
                    if (isImporting) {
                        isImporting = false
                        importWalletDialog.close()
                        importPathField.text = ""
                        importErrorLabel.text = ""
                        
                        // Show success message
                        errorLabel.text = "Wallet imported and loaded successfully: " + walletName
                        errorLabel.color = "green"
                    }
                }
                
                isLoading = false
                isCreating = false
                isLoadingWallet = false
                
                // Refresh wallet list to confirm wallet state
                refreshWallets()
            } else if (typeof result === "string" && result.trim() !== "") {
                // Handle string responses
                isLoading = false
                isCreating = false
                isLoadingWallet = false
            } else {
                // Empty result or other response
                isLoading = false
                isCreating = false
                isLoadingWallet = false
            }
        }
        function onErrorOccurred(errorMessage) {
            // Handle import/backup operation errors
            if (isImporting) {
                isImporting = false
                importErrorLabel.text = errorMessage
                importErrorLabel.color = "red"
                
                // If loadwallet failed after import, clean up the wallet directory
                if (isLoadingWallet && errorMessage.indexOf("loadwallet") >= 0) {
                    console.log("loadwallet failed after import, cleaning up")
                }
                return
            }
            if (isBackingUp) {
                isBackingUp = false
                errorLabel.text = errorMessage
                errorLabel.color = "red"
                return
            }
            
            // Ignore error -35 and "already loaded" errors for loadwallet 
            if (isLoadingWallet && (errorMessage.indexOf("already loaded") >= 0 || errorMessage.indexOf("already exists") >= 0 || errorMessage.indexOf("-35") >= 0)) {
                isLoadingWallet = false
                // Still refresh and set active wallet
                if (pendingCreatedWallet !== "") {
                    refreshWallets()
                } else {
                    isLoading = false
                    isCreating = false
                }
                return
            }
            isLoading = false
            isCreating = false
            isLoadingWallet = false
            pendingCreatedWallet = ""
            errorLabel.text = errorMessage
            errorLabel.color = "red"
        }
    }

    Connections {
        target: walletManager
        // Removed filesystem-based wallet import/backup handlers
        function onErrorOccurred(message) {
            errorLabel.text = message
            errorLabel.color = "red"
        }
        function onSuccessMessage(message) {
            errorLabel.text = message
            errorLabel.color = "green"
        }
        function onWalletExited() {
            // Wallet exit completed
            refreshWallets()
        }
        function onWalletDatImported(walletName) {
            // wallet.dat imported, now load it
            isLoadingWallet = true
            errorLabel.text = ""
            console.log("Loading imported wallet:", walletName)
            // Call CLI to load the imported wallet
            cliBridge.call("loadwallet", [walletName], "")
        }
        function onWalletDatBackedUp(backupPath) {
            lastBackupPath = backupPath
            errorLabel.text = "Backup completed successfully!\nSaved to: " + backupPath
            errorLabel.color = "green"
        }
    }

    // Timer for resetting auto-selection prevention
    Timer {
        id: autoSelectionResetTimer
        interval: 2000
        repeat: false
        onTriggered: {
            // RULE: Keep auto-selection disabled after initial startup selection
            // Only re-enable if user explicitly exits wallet
            if (settingsManager.activeWallet !== "") {
                preventAutoSelection = true
            }
        }
    }

    Component.onCompleted: {
        // On startup, refresh wallets to detect already loaded wallets
        refreshWallets()
        
        // Start auto-selection prevention timer
        autoSelectionResetTimer.start()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        Text {
            text: "Wallets"
            font.pixelSize: 24
            font.bold: true
        }

        GroupBox {
            Layout.fillWidth: true
            title: "Active Wallet"

            Rectangle {
                width: parent.width
                height: 40
                color: hasWallet ? "#f0f8ff" : "#f5f5f5"
                border.color: hasWallet ? "#4CAF50" : "#ccc"
                border.width: 1
                radius: 4

                Text {
                    anchors.centerIn: parent
                    text: hasWallet ? settingsManager.activeWallet : "No active wallet selected"
                    font.pixelSize: 16
                    font.bold: hasWallet
                    color: hasWallet ? "#2E7D32" : "#666"
                }
            }
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
                text: "Create Wallet"
                enabled: {
                    if (!settingsManager || !walletManager) return false
                    return settingsManager.qbitxCliPath !== "" && !isLoading && !isCreating && !isLoadingWallet && !walletBusy
                }
                onClicked: createWalletDialog.open()
            }

            Button {
                text: "Import wallet.dat"
                enabled: {
                    if (!settingsManager) return false
                    return settingsManager.qbitxCliPath !== "" && !isLoading && !isCreating && !isLoadingWallet
                }
                onClicked: importWalletDialog.open()
            }

            Button {
                text: "Backup wallet.dat"
                enabled: {
                    if (!settingsManager || !walletManager) return false
                    return hasWallet && !isBackingUp
                }
                onClicked: {
                    var walletName = settingsManager.activeWallet
                    if (!walletName) {
                        errorLabel.text = "No active wallet selected"
                        errorLabel.color = "red"
                        return
                    }
                    
                    isBackingUp = true
                    errorLabel.text = "Backing up wallet..."
                    errorLabel.color = "blue"
                    
                    var result = walletManager.backupWallet(walletName)
                    isBackingUp = false
                    
                    if (result.ok) {
                        lastBackupPath = result.backupPath
                        errorLabel.text = "Backup created at: " + result.backupPath
                        errorLabel.color = "green"
                    } else {
                        errorLabel.text = result.error || "Backup failed"
                        errorLabel.color = "red"
                    }
                }
            }

            Button {
                text: "Refresh"
                enabled: {
                    if (!settingsManager || !walletManager) return false
                    return settingsManager.qbitxCliPath !== "" && !isLoading && !isCreating && !isLoadingWallet && !walletBusy
                }
                onClicked: refreshWallets()
            }
        }

        Text {
            id: errorLabel
            color: "red"
            visible: text !== undefined && text !== null && text !== ""
            wrapMode: Text.Wrap
        }

        GroupBox {
            Layout.fillWidth: true
            title: "Last backup path"
            visible: lastBackupPath !== ""

            TextArea {
                Layout.fillWidth: true
                readOnly: true
                wrapMode: Text.Wrap
                text: lastBackupPath
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignCenter
            running: isLoading || isCreating || isLoadingWallet || walletBusy
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

                ListView {
                model: walletList
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 50
                    color: (settingsManager.activeWallet === String(modelData || "")) ? "#e3f2fd" : "white"
                    border.color: (settingsManager.activeWallet === String(modelData || "")) ? "#2196F3" : "#ccc"
                    border.width: (settingsManager.activeWallet === String(modelData || "")) ? 2 : 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: String(modelData || "default")
                            font.pixelSize: 14
                            font.bold: settingsManager.activeWallet === String(modelData || "")
                            verticalAlignment: Text.AlignVCenter
                            color: settingsManager.activeWallet === String(modelData || "") ? "#1976D2" : "black"
                        }

                        Text {
                            visible: settingsManager.activeWallet === String(modelData || "")
                            text: "ACTIVE"
                            font.pixelSize: 10
                            font.bold: true
                            color: "#4CAF50"
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: importWalletDialog
        title: "Import wallet.dat"
        width: 500
        height: 220

        ColumnLayout {
            anchors.fill: parent
            spacing: 15

            Text {
                text: "Enter path to wallet directory containing wallet.dat:"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            TextField {
                id: importPathField
                Layout.fillWidth: true
                placeholderText: "Path to wallet directory"
            }

            RowLayout {
                Layout.fillWidth: true
                
                Button {
                    text: "Paste Path"
                    onClicked: {
                        importPathField.paste()
                    }
                }
            }

            Text {
                id: importErrorLabel
                text: ""
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: "red"
                visible: text !== ""
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: isImporting ? "Importing..." : "Import"
                    Layout.fillWidth: true
                    enabled: !isImporting && importPathField.text !== ""
                    onClicked: {
                        var filePath = importPathField.text.trim()
                        
                        if (!filePath) {
                            importErrorLabel.text = "Please enter a path"
                            return
                        }
                        
                        isImporting = true
                        importErrorLabel.text = ""
                        
                        // Import wallet directory through WalletManager
                        var result = walletManager.importWalletDirectory(filePath)
                        if (!result.success) {
                            importErrorLabel.text = result.error
                            isImporting = false
                        } else {
                            // Success will be handled by walletDatImported signal
                            console.log("Import initiated for wallet:", result.walletName)
                            importErrorLabel.text = "Importing wallet: " + result.walletName + "..."
                            importErrorLabel.color = "blue"
                        }
                    }
                }

                Button {
                    text: "Cancel"
                    Layout.fillWidth: true
                    enabled: !isImporting
                    onClicked: {
                        importWalletDialog.close()
                        importPathField.text = ""
                        importErrorLabel.text = ""
                    }
                }
            }
        }
    }


    Dialog {
        id: createWalletDialog
        title: "Create Wallet"
        width: 400
        height: 200

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            TextField {
                id: newWalletName
                Layout.fillWidth: true
                placeholderText: "Wallet name"
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "Create"
                    Layout.fillWidth: true
                    enabled: !isCreating && !walletBusy
                    onClicked: {
                        if (newWalletName.text) {
                            isCreating = true
                            errorLabel.text = ""
                            errorLabel.color = "red"
                            // Call without wallet parameter (global operation)
                            cliBridge.call("createwallet", [newWalletName.text], "")
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


}
