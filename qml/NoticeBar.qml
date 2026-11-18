pragma ComponentBehavior: Bound

import QtQuick

// 页面顶部的提示条：琥珀底 + 一句话，可带一个按钮（比如"去检查"）。
// 只用于"需要注意但不紧急"的状态（离线、权限没开），紧急的事走全屏页。
Rectangle {
    id: bar

    property Theme theme
    property string message: ""
    property string actionText: ""

    signal actionClicked()

    implicitHeight: column.implicitHeight + bar.theme.gap * 1.2
    radius: bar.theme.radiusSmall
    color: bar.theme.amberSoft
    border.width: 2
    border.color: bar.theme.amber

    Column {
        id: column
        width: parent.width - bar.theme.gap * 2
        x: bar.theme.gap
        anchors.verticalCenter: parent.verticalCenter
        spacing: bar.theme.gapSmall

        AppText {
            width: parent.width
            theme: bar.theme
            role: "body"
            color: bar.theme.amberInk
            font.weight: Font.DemiBold
            text: "⚠ " + bar.message
            wrapMode: Text.WordWrap
        }

        AppButton {
            visible: bar.actionText !== ""
            width: parent.width
            height: bar.theme.touch
            theme: bar.theme
            text: bar.actionText
            kind: "secondary"
            onClicked: bar.actionClicked()
        }
    }

    Accessible.role: Accessible.AlertMessage
    Accessible.name: message
}
