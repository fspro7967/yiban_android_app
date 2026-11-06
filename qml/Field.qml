pragma ComponentBehavior: Bound

import QtQuick

// 大号输入框：标签 + 76dp 高输入区，聚焦时描边加粗。
Item {
    id: field

    property Theme theme
    property string label: ""
    property string placeholder: ""
    property alias text: input.text
    property alias inputMethodHints: input.inputMethodHints
    property alias echoMode: input.echoMode
    property int fontSize: theme ? theme.fontBody : 26
    property int maxLength: 40

    implicitHeight: column.implicitHeight

    function forceFocus() {
        input.forceActiveFocus()
    }

    Column {
        id: column
        width: field.width
        spacing: field.theme.px(8)

        AppText {
            theme: field.theme
            role: "label"
            color: field.theme.inkSoft
            text: field.label
        }

        Rectangle {
            width: parent.width
            height: field.theme.px(78)
            radius: field.theme.radiusSmall
            color: field.theme.surface
            border.width: input.activeFocus ? 3 : 2
            border.color: input.activeFocus ? field.theme.primary : field.theme.line

            AppText {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: field.theme.px(16)
                anchors.rightMargin: field.theme.px(16)
                anchors.verticalCenter: parent.verticalCenter
                visible: input.text.length === 0
                theme: field.theme
                role: "body"
                color: field.theme.inkSoft
                opacity: 0.65
                text: field.placeholder
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }

            TextInput {
                id: input
                anchors.fill: parent
                anchors.leftMargin: field.theme.px(16)
                anchors.rightMargin: field.theme.px(16)
                verticalAlignment: TextInput.AlignVCenter
                font.family: field.theme.fontFamily
                font.pixelSize: field.fontSize
                color: field.theme.ink
                selectionColor: field.theme.primary
                selectedTextColor: "#FFFFFF"
                maximumLength: field.maxLength
                clip: true
                selectByMouse: true
            }
        }
    }
}
