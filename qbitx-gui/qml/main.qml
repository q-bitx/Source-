import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QBitX 1.0
import "components" 1.0
import "pages" as Pages
import "theme" 1.0

ApplicationWindow {
    id: window
    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "Q-BitX Wallet"
    color: QbxTheme.bgApp

    property int currentPage: 0
    property bool shutdownRequested: false

    function hideToTray() {
        if (trayManager && trayManager.available)
            trayManager.hideToTray()
        else
            window.visible = false
    }

    function restoreFromTray() {
        window.visible = true
        if (trayManager)
            trayManager.restoreFromTray()
        else {
            window.show()
            window.raise()
            window.requestActivate()
        }
        triggerPageRefresh()
    }

    function exitGracefully() {
        shutdownRequested = true
        window.visible = false
        if (trayManager)
            trayManager.hideTrayIcon()
        if (nodeManager)
            nodeManager.requestGracefulShutdown()
    }

    onClosing: function(close) {
        if (shutdownRequested) {
            close.accepted = true
            return
        }
        close.accepted = false
        closeChoiceDialog.open()
    }

    Connections {
        target: trayManager
        function onOpenRequested() {
            restoreFromTray()
        }
        function onExitRequested() {
            exitGracefully()
        }
    }

    Dialog {
        id: closeChoiceDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(480, Math.max(420, window.width - 80))
        standardButtons: Dialog.NoButton
        header: null
        footer: null
        padding: 0

        background: Rectangle {
            color: QbxTheme.bgCard
            radius: QbxTheme.radiusMedium
            border.color: QbxTheme.border
            border.width: 1
        }

        contentItem: Item {
            implicitWidth: closeChoiceDialog.width
            implicitHeight: closeDialogBody.implicitHeight + 48

            ColumnLayout {
                id: closeDialogBody
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 24
                spacing: 12

                Text {
                    text: "QBitX Wallet"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: QbxTheme.accentGlow
                    Layout.fillWidth: true
                }

                Text {
                    text: "What do you want to do?"
                    font.pixelSize: 14
                    color: QbxTheme.textPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: 4
                }

                QbxButton {
                    text: "Minimize to tray"
                    primary: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    Layout.maximumHeight: 44
                    visible: trayManager && trayManager.available
                    onClicked: {
                        closeChoiceDialog.close()
                        hideToTray()
                    }
                }

                QbxButton {
                    text: "Stop node and exit"
                    primary: !(trayManager && trayManager.available)
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    Layout.maximumHeight: 44
                    onClicked: {
                        closeChoiceDialog.close()
                        exitGracefully()
                    }
                }

                QbxButton {
                    text: "Cancel"
                    compact: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 4
                    onClicked: closeChoiceDialog.close()
                }
            }
        }
    }

    function triggerPageRefresh() {
        if (currentPage === 0 && dashboardPage) dashboardPage.refresh()
        else if (currentPage === 1 && walletsPage) walletsPage.refresh()
        else if (currentPage === 2 && addressesPage) addressesPage.refresh()
        else if (currentPage === 3 && sendPage) sendPage.refresh()
        else if (currentPage === 4 && historyPage) historyPage.refresh()
        else if (currentPage === 5 && logsPage) logsPage.refresh()
        else if (currentPage === 6 && settingsPage) settingsPage.refresh()
    }

    function triggerRefreshAll() {
        if (dashboardPage) dashboardPage.refresh()
        if (walletsPage) walletsPage.refresh()
        if (addressesPage) addressesPage.refresh()
        if (sendPage) sendPage.refresh()
        if (historyPage) historyPage.refresh()
        if (logsPage) logsPage.refresh()
        if (settingsPage) settingsPage.refresh()
    }

    Connections {
        target: walletManager
        function onLoadedWalletsChanged() {
            if (addressesPage) addressesPage.refresh()
            if (sendPage) sendPage.refresh()
            if (historyPage) historyPage.refresh()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: QbxTheme.sidebarWidth
            Layout.fillHeight: true
            color: QbxTheme.bgSidebar

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: QbxTheme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    spacing: 10

                    Item {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48

                        Image {
                            id: sidebarLogo
                            anchors.fill: parent
                            source: "qrc:/QBitX/assets/sidebar_logo.png"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                            visible: status === Image.Ready
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: QbxTheme.radiusSmall
                            visible: sidebarLogo.status !== Image.Ready
                            color: Qt.rgba(QbxTheme.accent.r, QbxTheme.accent.g, QbxTheme.accent.b, 0.2)
                            border.color: QbxTheme.accent
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Q"
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                color: QbxTheme.accentGlow
                                visible: sidebarLogo.status !== Image.Ready
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text {
                            text: "Q-BitX"
                            font.pixelSize: 18
                            font.weight: Font.Bold
                            color: QbxTheme.textPrimary
                        }
                        Text {
                            text: "Desktop Wallet"
                            font.pixelSize: 11
                            color: QbxTheme.textMuted
                        }
                    }
                }

                SidebarButton {
                    text: "Dashboard"
                    active: currentPage === 0
                    onClicked: { currentPage = 0; triggerPageRefresh() }
                }
                SidebarButton {
                    text: "Wallets"
                    active: currentPage === 1
                    onClicked: { currentPage = 1; triggerPageRefresh() }
                }
                SidebarButton {
                    text: "Addresses"
                    active: currentPage === 2
                    onClicked: { currentPage = 2; triggerPageRefresh() }
                }
                SidebarButton {
                    text: "Send"
                    active: currentPage === 3
                    onClicked: { currentPage = 3; triggerPageRefresh() }
                }
                SidebarButton {
                    text: "History"
                    active: currentPage === 4
                    onClicked: { currentPage = 4; triggerPageRefresh() }
                }
                SidebarButton {
                    text: "Logs"
                    active: currentPage === 5
                    onClicked: { currentPage = 5; triggerPageRefresh() }
                }

                Item { Layout.fillHeight: true }

                SidebarButton {
                    text: "Settings"
                    active: currentPage === 6
                    onClicked: { currentPage = 6; triggerPageRefresh() }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: QbxTheme.bgContent
            clip: true

            StackLayout {
                anchors.fill: parent
                anchors.margins: 0
                currentIndex: currentPage

                Pages.Dashboard {
                    id: dashboardPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
                Pages.Wallets {
                    id: walletsPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
                Pages.Addresses {
                    id: addressesPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
                Pages.Send {
                    id: sendPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
                Pages.History {
                    id: historyPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
                Pages.Logs {
                    id: logsPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
                Pages.Settings {
                    id: settingsPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0
                }
            }
        }
    }
}
