import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QBitX 1.0
import "components" 1.0
import "pages" as Pages

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: "qbitx-gui"

    property int currentPage: 0

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

    // Startup: listwallets + trigger first-page refresh
    Component.onCompleted: {
        Qt.callLater(function() {
            if (settingsManager.effectiveQbitxCliPath() !== "" && settingsManager.checkCliAvailable())
                cliBridge.call("listwallets", [], "")
            triggerPageRefresh()
        })
    }

    // REMOVED: Global auto-selection moved entirely to Wallets.qml for monotonic control
    // REMOVED: Input diagnosis probe (fullscreen Item+MouseArea) — it was blocking all clicks; no MouseArea in main.qml.

    // Optional debug label: does NOT capture input (no MouseArea/TapHandler). Off by default.
    Text {
        visible: false
        enabled: false
        text: "Top item"
        font.pixelSize: 10
        color: "#888"
        x: 8
        y: 8
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar navigation
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: "#1a1a1a"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: "Q-BitX"
                    font.pixelSize: 20
                    font.bold: true
                    color: "white"
                    padding: 10
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
            }
        }

        // Main content area
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentPage

            Pages.Dashboard { id: dashboardPage }
            Pages.Wallets { id: walletsPage }
            Pages.Addresses { id: addressesPage }
            Pages.Send { id: sendPage }
            Pages.History { id: historyPage }
            Pages.Logs { id: logsPage }
            Pages.Settings { id: settingsPage }
        }
    }
}
