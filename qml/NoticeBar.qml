pragma ComponentBehavior: Bound

import QtQuick

// 页面顶部的提示条：琥珀底 + 一句话，可带一个按钮（比如"去检查"）。
// 只用于"需要注意但不紧急"的状态（离线、权限没开），紧急的事走全屏页。
Rectangle {
    id: bar

    property Theme theme
    property string message: ""
    property string actionText: ""
    property string iconName: "alert"

    signal actionClicked()

    implicitHeight: column.implicitHeight + bar.theme.gap
    radius: bar.theme.radiusSmall
    color: bar.theme.amberSoft
    border.width: 1
    border.color: bar.theme.amberBar

    Column {
        id: column
        width: parent.width - bar.theme.gap * 2
        x: bar.theme.gap
        anchors.verticalCenter: parent.verticalCenter
        spacing: bar.theme.gapSmall

        Row {
            width: parent.width
            spacing: bar.theme.gapSmall

            Icon {
                id: alertIcon
                y: Math.max(0, (bar.theme.fontLabel * 1.3 - height) / 2)
                name: bar.iconName
                size: Math.round(bar.theme.fontLabel * 1.15)
                color: bar.theme.amberInk
            }

            AppText {
                width: parent.width - alertIcon.width - bar.theme.gapSmall
                theme: bar.theme
                role: "label"
                color: bar.theme.amberInk
                font.weight: Font.DemiBold
                text: bar.message
                wrapMode: Text.WordWrap
                lineHeight: 1.0
            }
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
