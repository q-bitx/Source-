import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QBitX 1.0

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: "qbitx-gui"

    property int currentPage: 0

    // Startup logic to check for loaded wallets
    Component.onCompleted: {
        // Give a small delay to ensure all components are initialized
        Qt.callLater(function() {
            if (settingsManager.qbitxCliPath !== "") {
                // Check if any wallets are already loaded
                cliBridge.call("listwallets", [], "")
            }
        })
    }

    // REMOVED: Global auto-selection moved entirely to Wallets.qml for monotonic control
    // The Wallets.qml will handle ALL wallet selection with proper monotonic state guards

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar navigation
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: "#2b2b2b"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5

                Text {
                    Layout.fillWidth: true
                    text: "qbitx-gui"
                    font.pixelSize: 20
                    font.bold: true
                    color: "white"
                    padding: 10
                }

                Button {
                    Layout.fillWidth: true
                    text: "Dashboard"
                    highlighted: currentPage === 0
                    onClicked: currentPage = 0
                }

                Button {
                    Layout.fillWidth: true
                    text: "Wallets"
                    highlighted: currentPage === 1
                    onClicked: currentPage = 1
                }

                Button {
                    Layout.fillWidth: true
                    text: "Addresses"
                    highlighted: currentPage === 2
                    onClicked: currentPage = 2
                }

                Button {
                    Layout.fillWidth: true
                    text: "Balance"
                    highlighted: currentPage === 3
                    onClicked: currentPage = 3
                }

                Button {
                    Layout.fillWidth: true
                    text: "Send"
                    highlighted: currentPage === 4
                    onClicked: currentPage = 4
                }

                Button {
                    Layout.fillWidth: true
                    text: "History"
                    highlighted: currentPage === 5
                    onClicked: currentPage = 5
                }

                Button {
                    Layout.fillWidth: true
                    text: "Settings"
                    highlighted: currentPage === 6
                    onClicked: currentPage = 6
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Main content area
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentPage

            Dashboard { }
            Wallets { }
            Addresses { }
            Balance { }
            Send { }
            History { }
            Settings { }
        }
    }
}
