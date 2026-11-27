pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 管理后台的"外设板"卡片：扫描、绑定、看链路通不通。
// 外设板带人体雷达、灯板和确认键，不负责声音。状态全部来自 BoardVM，这里只发意图。
Rectangle {
    id: card
    objectName: "boardCard"

    property Theme theme

    // 扫描结果一次只列信号最强的几台，免得把页面撑得很长。
    readonly property int deviceLimit: 6
    // 还没绑板、也没在连：这时才显示扫描入口。
    readonly property bool choosing: BoardVM.linkState === "idle" && !BoardVM.bound

    function presenceText() {
        var p = BoardVM.presence
        if (p.present === undefined)
            return ""
        if (!p.present)
            return "雷达：没有人"
        var text = p.moving ? "雷达：有人在活动" : "雷达：有人，静止"
        if (!p.moving && p.still_s !== undefined)
            text += " " + Math.round(p.still_s / 60) + " 分钟"
        if (p.dist_cm !== undefined)
            text += "，距离约 " + (p.dist_cm / 100).toFixed(1) + " 米"
        return text
    }

    height: column.implicitHeight + theme.gap * 1.5
    radius: theme.radius
    color: theme.surface
    border.width: 1
    border.color: theme.cardLine

    Column {
        id: column
        width: parent.width - card.theme.gap * 2
        x: card.theme.gap
        y: card.theme.px(12)
        spacing: card.theme.gapSmall

        RowLayout {
            width: parent.width
            spacing: card.theme.gapSmall

            AppText {
                theme: card.theme
                role: "body"
                text: "蓝牙连接"
            }

            AppText {
                objectName: "boardState"
                Layout.fillWidth: true
                theme: card.theme
                role: "label"
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
                font.weight: Font.DemiBold
                color: BoardVM.ready ? card.theme.primary
                    : BoardVM.linkState === "unsupported" ? card.theme.danger
                    : card.choosing ? card.theme.inkSoft
                    : card.theme.amberInk
                text: BoardVM.statusText
            }
        }

        AppText {
            width: parent.width
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "外设板带人体雷达、灯板和确认键。提醒的声音仍然从手机或音箱出。"
            wrapMode: Text.WordWrap
        }

        // ---- 还没绑定：扫描并挑一台 ----

        AppButton {
            objectName: "boardScan"
            width: parent.width
            height: card.theme.touch
            visible: card.choosing
            theme: card.theme
            text: BoardVM.scanning ? "停止扫描" : "扫描附近的外设板"
            kind: BoardVM.scanning ? "secondary" : "primary"
            onClicked: BoardVM.scanning ? BoardVM.stopScan() : BoardVM.startScan()
        }

        AppText {
            width: parent.width
            visible: card.choosing && BoardVM.scanning && BoardVM.devices.length === 0
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "正在找…请确认外设板已经通电。只会列出外设板，别的蓝牙设备不显示。"
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: card.choosing ? BoardVM.devices.slice(0, card.deviceLimit) : []

            delegate: AppButton {
                id: deviceButton

                required property var modelData

                width: column.width
                theme: card.theme
                kind: "secondary"
                text: deviceButton.modelData.name
                // rssi 为 0 是没量到，不显示。
                subtitle: (deviceButton.modelData.rssi < 0 ? "信号 " + deviceButton.modelData.rssi + " dBm · " : "")
                    + "点这里连接"
                onClicked: BoardVM.connectDevice(deviceButton.modelData.id)
            }
        }

        AppText {
            width: parent.width
            visible: card.choosing && BoardVM.devices.length > card.deviceLimit
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "还有 " + (BoardVM.devices.length - card.deviceLimit) + " 台信号更弱的没有列出。"
            wrapMode: Text.WordWrap
        }

        // ---- 已连接：链路通不通，一眼看得出 ----

        AppText {
            objectName: "boardCounters"
            width: parent.width
            visible: BoardVM.ready
            theme: card.theme
            role: "label"
            text: "已发出 " + BoardVM.sentCount + " 条（含每 5 秒一次的心跳），收到 " + BoardVM.receivedCount + " 条"
            wrapMode: Text.WordWrap
        }

        AppText {
            width: parent.width
            visible: BoardVM.ready && card.presenceText().length > 0
            theme: card.theme
            role: "label"
            color: card.theme.primary
            text: card.presenceText()
            wrapMode: Text.WordWrap
        }

        AppText {
            objectName: "boardLastMessage"
            width: parent.width
            visible: BoardVM.lastMessage.length > 0
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "最近收到（" + Fmt.logTime(BoardVM.lastMessageTs) + "）：" + BoardVM.lastMessage
            wrapMode: Text.WrapAnywhere
        }

        AppButton {
            objectName: "boardForget"
            width: parent.width
            height: card.theme.touch
            visible: !card.choosing
            theme: card.theme
            text: BoardVM.bound ? "解除绑定" : "取消连接"
            kind: "secondary"
            onClicked: BoardVM.forget()
        }

        AppText {
            width: parent.width
            visible: BoardVM.bound
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "已经记住这块板，断线后会自动连回。要换一块板，先解除绑定。"
            wrapMode: Text.WordWrap
        }

        AppText {
            width: parent.width
            visible: BoardVM.detail.length > 0
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "调试信息：" + BoardVM.detail
            wrapMode: Text.Wrap
        }
    }
}
