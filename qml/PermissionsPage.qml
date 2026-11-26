pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 权限检查：漏开一项就可能"到点不响、锁屏不亮、呼救打不出去"。
// 能查到的项读 DeviceVM.permissionDiagnostics（I3：{key,label,granted,canOpenSettings}），
// 能跳转的直接调 DeviceVM.openSystemSettings(key) 去对应的系统设置页；
// DeviceVM 每 2 秒重查一次，从设置页回来不用手动刷新。
// 查不到的（HyperOS 等各家自己的自启动 / 后台弹出界面开关）只能写清楚去哪里开，请用户手动确认。
Item {
    id: page

    property Theme theme

    signal back()

    readonly property bool onAndroid: Qt.platform.os === "android"
    readonly property var diagnostics: DeviceVM.permissionDiagnostics

    // 每一项为什么要开；key 对应 permissionDiagnostics 的 key。没有写的项只显示名字。
    readonly property var reasons: ({
        "notifications": "不开的话，到点提醒不会弹出来。",
        "exactAlarm": "不开的话，提醒可能晚几分钟才响。",
        "fullscreenIntent": "不开的话，锁屏时只响铃不亮屏。",
        "battery": "不加入的话，手机省电时可能把颐伴关掉，夜里的提醒就不响了。",
        "phone": "紧急呼救时要用它给家人打电话。",
        "sms": "紧急呼救时同时给家人发短信。",
        "microphone": "和语音助手说话时要用。不用语音可以不开。"
    })

    readonly property var manual: [
        { label: "允许自启动", how: "系统设置 → 应用设置 → 颐伴 → 打开“自启动”，重启手机后提醒才不会丢。" },
        { label: "后台弹出界面", how: "系统设置 → 应用设置 → 颐伴 → 其他权限 → 允许“后台弹出界面”。" },
        { label: "锁屏显示", how: "同一页里允许“锁屏显示”，锁屏时提醒页才能盖在锁屏上面。" }
    ]

    readonly property int missingCount: Fmt.missingPermissions(page.diagnostics)

    function openSettings(key) {
        if (!DeviceVM.openSystemSettings(key))
            Notify.info("打不开系统设置，请到 系统设置 → 应用设置 → 颐伴 里手动开启")
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
                    text: page.missingCount === 0 ? "必需的权限都开好了"
                        : "还有 " + page.missingCount + " 项必需的权限没开"
                    wrapMode: Text.WordWrap
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "开完回到这里，两秒内会自动刷新。"
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: page.diagnostics

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property bool ok: modelData.granted === true
                        readonly property bool optional: Fmt.optionalPermission(modelData.key)

                        objectName: "permission_" + modelData.key
                        width: list.width
                        height: cardColumn.implicitHeight + page.theme.gap * 1.5
                        radius: page.theme.radius
                        color: card.ok || card.optional ? page.theme.surface : page.theme.amberSoft
                        border.width: 2
                        border.color: card.ok || card.optional ? page.theme.line : page.theme.amber

                        Column {
                            id: cardColumn
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
                                    text: card.modelData.label + (card.optional ? "（可选）" : "")
                                    wrapMode: Text.WordWrap
                                }

                                AppText {
                                    theme: page.theme
                                    role: "body"
                                    font.weight: Font.DemiBold
                                    color: card.ok ? page.theme.primary
                                        : card.optional ? page.theme.inkSoft
                                        : page.theme.amberInk
                                    text: card.ok ? "✅ 已开启" : "❌ 未开启"
                                }
                            }

                            AppText {
                                width: parent.width
                                visible: text !== ""
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: page.reasons[card.modelData.key] || ""
                                wrapMode: Text.WordWrap
                            }

                            // 已开启的也留一个入口：看护人想关掉或核对时不用自己去设置里找。
                            AppButton {
                                visible: card.modelData.canOpenSettings === true
                                width: parent.width
                                height: page.theme.touch
                                theme: page.theme
                                text: card.ok ? "去设置查看" : "去设置开启"
                                kind: card.ok || card.optional ? "secondary" : "primary"
                                onClicked: page.openSettings(card.modelData.key)
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
                        border.width: 1
                        border.color: page.theme.cardLine

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
