pragma ComponentBehavior: Bound

import QtQuick

// 5 秒撤销条：误点确认、误记一杯水时留一条退路（R3-9）。
Rectangle {
    id: bar

    property Theme theme
    property bool active: false
    property string message: ""
    property int secondsLeft: 5

    signal undo()

    visible: active
    implicitHeight: theme.px(82)
    radius: theme.radiusSmall
    color: theme.dark
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.15)

    Row {
        anchors.fill: parent
        anchors.margins: bar.theme.px(10)
        spacing: bar.theme.px(10)

        AppText {
            width: parent.width - undoButton.width - bar.theme.px(10)
            height: parent.height
            theme: bar.theme
            role: "body"
            color: bar.theme.darkInk
            text: bar.message
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
        }

        AppButton {
            id: undoButton
            width: bar.theme.px(112)
            height: parent.height
            theme: bar.theme
            text: "撤销"
            kind: "light"
            onClicked: bar.undo()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 1
        width: Math.max(0, (parent.width - 2) * Math.min(1, bar.secondsLeft / 5))
        height: bar.theme.px(4)
        radius: height / 2
        color: bar.theme.reminderAccent
    }

    Accessible.role: Accessible.AlertMessage
    Accessible.name: message
}
