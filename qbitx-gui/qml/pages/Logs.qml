import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: logsPage

    property bool atBottom: true

    function refresh() {
        // Nothing to refresh; log text is live via logManager
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Text {
            text: "Logs"
            font.pixelSize: 22
            font.bold: true
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: "Copy all"
                onClicked: logManager.copyToClipboard()
            }
            Button {
                text: "Open Folder"
                onClicked: logManager.openLogsFolder()
            }
            Button {
                text: "Clear"
                onClicked: logManager.clear()
            }
            Item { Layout.fillWidth: true }
        }

        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: logArea
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.NoWrap
                font.pixelSize: 12
                font.family: "Consolas,Courier New,monospace"
                text: logManager ? logManager.logText : ""

                Connections {
                    target: logManager
                    function onLogTextChanged() {
                        if (logsPage.atBottom)
                            Qt.callLater(scrollView.scrollToBottom)
                    }
                }
            }

            Component.onCompleted: {
                if (contentItem) {
                    contentItem.contentYChanged.connect(updateAtBottom)
                    contentItem.contentHeightChanged.connect(updateAtBottom)
                }
            }

            function updateAtBottom() {
                if (!contentItem) return
                var fy = contentItem.contentY
                var fh = contentItem.contentHeight
                var vh = contentItem.height
                logsPage.atBottom = (fh <= vh) || (fy >= fh - vh - 2)
            }

            function scrollToBottom() {
                if (!contentItem) return
                contentItem.contentY = contentItem.contentHeight - contentItem.height
                if (contentItem.contentY < 0)
                    contentItem.contentY = 0
            }
        }
    }
}
