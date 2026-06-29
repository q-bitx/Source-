import QtQuick
import QtQuick.Controls
import "../theme"

ComboBox {
    id: comboRoot

    implicitHeight: 38
    font.pixelSize: 13
    readonly property int popupRowWidth: Math.max(width - 8, 260)

    function rowText(value, index) {
        var item = value
        if (item === undefined || item === null) {
            if (index >= 0 && index < count) {
                var m = model
                if (m !== undefined && m !== null) {
                    if (typeof m.get === "function" && index < m.count)
                        item = m.get(index)
                    else if (Array.isArray(m) && index < m.length)
                        item = m[index]
                }
            }
        }
        if (item === undefined || item === null)
            return ""
        if (typeof item === "string" || typeof item === "number")
            return String(item)
        if (typeof item === "object") {
            if (textRole && textRole !== "" && item[textRole] !== undefined && item[textRole] !== null)
                return String(item[textRole])
            if (item.label !== undefined && item.label !== null)
                return String(item.label)
            if (item.text !== undefined && item.text !== null)
                return String(item.text)
            if (item.address !== undefined && item.address !== null)
                return String(item.address)
        }
        return String(item)
    }

    onCountChanged: {
        if (count === 0) {
            if (currentIndex !== -1)
                currentIndex = -1
            return
        }
        if (currentIndex < 0 || currentIndex >= count)
            currentIndex = 0
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: 12
        text: comboRoot.displayText
        font.pixelSize: comboRoot.font.pixelSize
        color: QbxTheme.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: comboRoot.textRole ? Text.ElideMiddle : Text.ElideRight
    }

    background: Rectangle {
        radius: QbxTheme.radiusSmall
        color: QbxTheme.bgInput
        border.color: comboRoot.activeFocus ? QbxTheme.accent : QbxTheme.border
        border.width: 1
    }

    popup: Popup {
        id: comboPopup
        y: comboRoot.height + 2
        width: Math.max(comboRoot.width, 280)
        padding: 4
        implicitHeight: popupList.implicitHeight + padding * 2

        contentItem: ListView {
            id: popupList
            clip: true
            width: comboPopup.width - comboPopup.padding * 2
            implicitHeight: Math.min(contentHeight, 280)
            model: comboRoot.popup.visible ? comboRoot.delegateModel : null
            currentIndex: comboRoot.highlightedIndex
            spacing: 2

            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            radius: QbxTheme.radiusSmall
            color: QbxTheme.bgCard
            border.color: QbxTheme.border
            border.width: 1
        }
    }

    delegate: ItemDelegate {
        id: rowDelegate
        width: comboRoot.popupRowWidth
        height: 40
        highlighted: comboRoot.highlightedIndex === index

        readonly property var rowValue: {
            if (typeof modelData !== "undefined" && modelData !== null)
                return modelData
            if (typeof model === "object" && model !== null && !(model instanceof Array))
                return model
            return null
        }
        readonly property string rowLabel: comboRoot.rowText(rowValue, index)

        contentItem: Text {
            text: rowDelegate.rowLabel
            font.pixelSize: comboRoot.font.pixelSize
            color: rowDelegate.highlighted ? QbxTheme.textPrimary : QbxTheme.textSecondary
            verticalAlignment: Text.AlignVCenter
            elide: comboRoot.textRole ? Text.ElideMiddle : Text.ElideRight
            leftPadding: 8
            rightPadding: 8
            width: rowDelegate.width
        }

        background: Rectangle {
            radius: 4
            color: rowDelegate.highlighted ? QbxTheme.bgActive
                  : (rowDelegate.hovered ? QbxTheme.bgHover : "transparent")
        }
    }
}
