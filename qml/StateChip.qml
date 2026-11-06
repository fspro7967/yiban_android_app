pragma ComponentBehavior: Bound

import QtQuick

// 状态胶囊：文字 + 颜色双重表达，不依赖颜色单独传意。
Rectangle {
    id: chip

    property Theme theme
    property string status: "pending"

    readonly property string labelText: status === "confirmed" ? "已确认"
        : status === "notified" ? "待确认"
        : status === "snoozed" ? "已推迟"
        : status === "missed" ? "已错过"
        : status === "escalated" ? "已通知家人"
        : "待提醒"

    readonly property color chipBg: status === "confirmed" ? theme.primarySoft
        : status === "missed" || status === "escalated" ? theme.dangerSoft
        : status === "notified" || status === "snoozed" ? theme.amberSoft
        : theme.surfaceAlt

    readonly property color chipFg: status === "confirmed" ? theme.primaryInk
        : status === "missed" || status === "escalated" ? theme.dangerInk
        : status === "notified" || status === "snoozed" ? theme.amberInk
        : theme.inkSoft

    implicitWidth: chipLabel.implicitWidth + theme.px(24)
    implicitHeight: chipLabel.implicitHeight + theme.px(12)
    radius: height / 2
    color: chipBg

    AppText {
        id: chipLabel
        anchors.centerIn: parent
        theme: chip.theme
        role: "label"
        color: chip.chipFg
        font.weight: Font.DemiBold
        text: chip.labelText
        wrapMode: Text.NoWrap
    }
}
