pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 权限检查：漏开一项就可能"到点不响、锁屏不亮、呼救打不出去"。
// 能查到的项读 DeviceVM.permissions（中文标签列表，已授权的才在里面），
// 查不到的（各家手机自己的后台/自启动开关）只能写清楚去哪里开，请用户手动确认。
Item {
    id: page

    property Theme theme

    signal back()

    readonly property bool onAndroid: Qt.platform.os === "android"
    readonly property var granted: DeviceVM.permissions

    // action：点"去开启"时调用的 DeviceVM 方法名；空串表示没有直接入口。
    readonly property var checkable: [
        { label: "通知", why: "不开的话，到点提醒不会弹出来。", action: "requestNotificationPermission" },
        { label: "精确闹钟", why: "不开的话，提醒可能晚几分钟才响。", action: "requestExactAlarmPermission" },
        { label: "锁屏提醒", why: "不开的话，锁屏时只响铃不亮屏。", action: "requestFullScreenPermission" },
        { label: "电话", why: "紧急呼救时要用它给家人打电话。配对时会请求。", action: "" },
        { label: "短信", why: "紧急呼救时同时给家人发短信。配对时会请求。", action: "" }
    ]

    readonly property var manual: [
        { label: "不限制后台耗电", how: "系统设置 → 应用设置 → 颐伴 → 省电策略 → 选“无限制”。" },
        { label: "允许自启动", how: "系统设置 → 应用设置 → 颐伴 → 打开“自启动”，重启手机后提醒才不会丢。" },
        { label: "后台弹出界面", how: "系统设置 → 应用设置 → 颐伴 → 其他权限 → 允许“后台弹出界面”。" },
        { label: "锁屏显示", how: "同一页里允许“锁屏显示”，锁屏时提醒页才能盖在锁屏上面。" }
    ]

    function isGranted(label) {
        return page.granted.indexOf(label) >= 0
    }

    function request(action) {
        if (action === "requestNotificationPermission")
            DeviceVM.requestNotificationPermission()
        else if (action === "requestExactAlarmPermission")
            DeviceVM.requestExactAlarmPermission()
        else if (action === "requestFullScreenPermission")
            DeviceVM.requestFullScreenPermission()
    }

    readonly property int missingCount: {
        var n = 0
        for (var i = 0; i < page.checkable.length; ++i)
            if (!page.isGranted(page.checkable[i].label))
                ++n
        return n
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        HeaderBar {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.topMargin: page.theme.gapSmall
            theme: page.theme
            title: "权限检查"
            subtitle: "保证提醒能响"
            showBack: true
            onBack: page.back()
        }

        Flickable {
            id: scroll
            objectName: "permissionsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            contentWidth: width
            contentHeight: list.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: list
                width: scroll.width
                spacing: page.theme.gap
                topPadding: page.theme.gapSmall
                bottomPadding: page.theme.gap * 2

                NoticeBar {
                    width: list.width
                    visible: !page.onAndroid
                    theme: page.theme
                    message: "电脑版没有系统权限，这一页只是预览，请在手机上检查。"
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "heading"
                    color: page.missingCount === 0 ? page.theme.primaryInk : page.theme.amberInk
                    text: page.missingCount === 0 ? "能查到的权限都开好了"
                        : "还有 " + page.missingCount + " 项没开"
                    wrapMode: Text.WordWrap
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "开完回到这里，几秒钟后会自动刷新。"
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: page.checkable

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property bool ok: page.isGranted(modelData.label)

                        width: list.width
                        height: cardColumn.implicitHeight + page.theme.gap * 1.5
                        radius: page.theme.radius
                        color: card.ok ? page.theme.surface : page.theme.amberSoft
                        border.width: 2
                        border.color: card.ok ? page.theme.line : page.theme.amber

                        Column {
                            id: cardColumn
                            width: parent.width - page.theme.gap * 2
                            x: page.theme.gap
                            y: page.theme.px(12)
                            spacing: page.theme.gapSmall

                            RowLayout {
                                width: parent.width

                                AppText {
                                    theme: page.theme
                                    role: "heading"
                                    text: card.modelData.label
                                }

                                Item { Layout.fillWidth: true }

                                AppText {
                                    theme: page.theme
                                    role: "body"
                                    font.weight: Font.DemiBold
                                    color: card.ok ? page.theme.primary : page.theme.amberInk
                                    text: card.ok ? "✅ 已开启" : "❌ 未开启"
                                }
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: card.modelData.why
                                wrapMode: Text.WordWrap
                            }

                            AppButton {
                                visible: !card.ok && card.modelData.action !== ""
                                width: parent.width
                                height: page.theme.touch
                                theme: page.theme
                                text: "去开启"
                                kind: "primary"
                                onClicked: page.request(card.modelData.action)
                            }
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "下面几项手机不让应用查询，请照着说明手动确认"
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: page.manual

                    delegate: Rectangle {
                        id: manualCard
                        required property var modelData

                        width: list.width
                        height: manualColumn.implicitHeight + page.theme.gap * 1.5
                        radius: page.theme.radius
                        color: page.theme.surface
                        border.width: 2
                        border.color: page.theme.line

                        Column {
                            id: manualColumn
                            width: parent.width - page.theme.gap * 2
                            x: page.theme.gap
                            y: page.theme.px(12)
                            spacing: page.theme.gapSmall

                            RowLayout {
                                width: parent.width

                                AppText {
                                    Layout.fillWidth: true
                                    theme: page.theme
                                    role: "heading"
                                    text: manualCard.modelData.label
                                    wrapMode: Text.WordWrap
                                }

                                AppText {
                                    theme: page.theme
                                    role: "label"
                                    font.weight: Font.DemiBold
                                    color: page.theme.inkSoft
                                    text: "请手动确认"
                                }
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: manualCard.modelData.how
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
