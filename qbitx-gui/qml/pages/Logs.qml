import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components" 1.0
import "../theme" 1.0

Page {
    id: logsPage
    background: Rectangle { color: "transparent" }

    property bool atBottom: true

    function refresh() {
    }

    QbxPageLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            QbxSectionTitle { text: "Logs" }
            Item { Layout.fillWidth: true }
            QbxButton {
                text: "Copy all"
                compact: true
                onClicked: logManager.copyToClipboard()
            }
            QbxButton {
                text: "Open folder"
                compact: true
                onClicked: logManager.openLogsFolder()
            }
            QbxButton {
                text: "Clear"
                compact: true
                onClicked: logManager.clear()
            }
        }

        QbxCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Application log"

            ScrollView {
                id: scrollView
                Layout.fillWidth: true
                Layout.preferredHeight: 400
                clip: true

                TextArea {
                    id: logArea
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    font.pixelSize: 12
                    font.family: "Consolas,Courier New,monospace"
                    color: QbxTheme.textPrimary
                    text: logManager ? logManager.logText : ""
                    background: Rectangle {
                        color: QbxTheme.bgInput
                        radius: QbxTheme.radiusSmall
                        border.color: QbxTheme.border
                    }

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
}
