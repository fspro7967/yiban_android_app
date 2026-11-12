pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 首页只保留 4 个大入口 + 一条 SOS，避免老人面对一屏小按钮（D14）。
Item {
    id: page

    property Theme theme

    signal openPage(string page)
    signal sosActivated()

    readonly property var todayNext: ReminderVM.current
    readonly property bool hasToday: todayNext && todayNext.occurrenceId !== undefined
    readonly property var futureNext: ReminderVM.next
    readonly property bool hasFuture: futureNext && futureNext.occurrenceId !== undefined
    readonly property var next: hasToday ? todayNext : futureNext
    readonly property bool hasNext: hasToday || hasFuture
    readonly property int planCount: PlanVM.plans.length
    readonly property bool hasPlans: planCount > 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.topMargin: page.theme.px(10)
            spacing: page.theme.gapSmall

            AppText {
                Layout.fillWidth: true
                theme: page.theme
                role: "title"
                text: ProfileVM.elderName.length > 0
                    ? Fmt.greeting + "，" + ProfileVM.elderName
                    : "你好"
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }

            AppText {
                Layout.fillWidth: true
                theme: page.theme
                role: "label"
                color: page.theme.inkSoft
                text: Fmt.dateText + "  " + Fmt.clockText
            }
        }

        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            contentWidth: width
            contentHeight: body.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: body
                width: scroll.width
                spacing: page.theme.px(8)
                topPadding: page.theme.px(4)
                bottomPadding: page.theme.px(8)

                Rectangle {
                    width: body.width
                    height: heroBody.implicitHeight + page.theme.px(8) * 2
                    radius: page.theme.radius
                    color: !page.hasNext ? page.theme.primarySoft
                        : page.next.dueNow ? page.theme.amberSoft
                        : page.theme.surface
                    border.width: 2
                    border.color: !page.hasNext ? page.theme.primary
                        : page.next.dueNow ? page.theme.amber
                        : page.theme.line

                    Column {
                        id: heroBody
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.gapSmall
                        spacing: page.theme.px(4)

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: page.hasNext && page.next.dueNow ? page.theme.amberInk : page.theme.inkSoft
                                text: !page.hasNext
                                    ? (page.hasPlans ? "今天" : "开始使用")
                                    : page.hasToday ? (page.next.state === "snoozed" ? "已推迟到" : "下一条提醒")
                                    : (page.next.dayLabel || "下一条提醒")
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "label"
                                font.weight: Font.DemiBold
                                color: !page.hasNext ? page.theme.primaryInk
                                    : page.next.dueNow ? page.theme.amberInk
                                    : page.theme.inkSoft
                                text: {
                                    if (!page.hasNext)
                                        return page.hasPlans ? "全部完成" : "0 条提醒"
                                    if (page.next.dueNow)
                                        return "现在"
                                    // 一律用目标时间戳和实时 nowMs 现算：current 里的 diffMinutes
                                    // 只在状态变化时刷新，时间走了它不会跟着变。
                                    var target = page.next.dueTs
                                    if (page.hasToday && page.next.state === "snoozed"
                                            && page.next.snoozeUntilTs > ReminderVM.nowMs)
                                        target = page.next.snoozeUntilTs
                                    var m = Math.max(0, Math.ceil((target - ReminderVM.nowMs) / 60000))
                                    if (m <= 0)
                                        return "现在"
                                    return Fmt.countdownText(m)
                                }
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "display"
                            font.pixelSize: page.theme.px(46)
                            color: page.hasNext ? page.theme.primaryInk : page.theme.primary
                            text: (page.hasNext && page.next && page.next.displayTime !== undefined) ? page.next.displayTime
                                : (page.hasPlans ? "✓" : "＋")
                            wrapMode: Text.NoWrap
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "heading"
                            text: (page.hasNext && page.next && page.next.title !== undefined) ? page.next.title
                                : (page.hasPlans ? "今天的提醒都完成啦" : "还没有提醒")
                            wrapMode: Text.WordWrap
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.inkSoft
                            text: page.hasNext && page.next ? (page.next.dose || "")
                                : (page.hasPlans
                                    ? "记得喝水、活动一下，晚上再测一次血压"
                                    : "点下面「提醒」，再点右上角「新建」，添加第一条吃药或喝水提醒。")
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: body.width
                    height: progressBody.implicitHeight + page.theme.px(8) * 2
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line
                    visible: page.hasPlans && ReminderVM.todayTotal > 0

                    Column {
                        id: progressBody
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.gapSmall
                        spacing: page.theme.px(4)

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "今天已完成"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "heading"
                                color: page.theme.primary
                                text: ReminderVM.todayDone + " / " + ReminderVM.todayTotal
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: page.theme.px(14)
                            radius: height / 2
                            color: page.theme.surfaceAlt

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, ReminderVM.todayDone / Math.max(1, ReminderVM.todayTotal)))
                                height: parent.height
                                radius: height / 2
                                color: page.theme.primary

                                Behavior on width {
                                    NumberAnimation { duration: 220 }
                                }
                            }
                        }
                    }
                }

                GridLayout {
                    width: body.width
                    columns: 2
                    columnSpacing: page.theme.gap
                    rowSpacing: page.theme.gap

                    NavTile {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.px(88)
                        theme: page.theme
                        glyph: "提"
                        title: "提醒"
                        subtitle: ReminderVM.pendingCount + " 条待办"
                        tint: page.theme.tileAmber
                        badgeColor: page.theme.amberInk
                        onClicked: page.openPage("reminders")
                    }

                    NavTile {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.px(88)
                        theme: page.theme
                        glyph: "记"
                        title: "记录"
                        subtitle: "喝水、睡眠"
                        tint: page.theme.tileGreen
                        badgeColor: page.theme.primaryInk
                        onClicked: page.openPage("record")
                    }

                    NavTile {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.px(88)
                        theme: page.theme
                        glyph: "统"
                        title: "统计"
                        subtitle: "完成率 " + Fmt.averageRate(RecordVM.stats7) + "%"
                        tint: page.theme.tileBlue
                        badgeColor: "#244A7A"
                        onClicked: page.openPage("stats")
                    }

                    NavTile {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.px(88)
                        theme: page.theme
                        glyph: "设"
                        title: "设置"
                        subtitle: "字号、语音"
                        tint: page.theme.tileSand
                        badgeColor: page.theme.inkSoft
                        onClicked: page.openPage("settings")
                    }
                }
            }
        }

        SosButton {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.bottomMargin: page.theme.gap
            Layout.preferredHeight: page.theme.px(72)
            theme: page.theme
            onActivated: page.sosActivated()
        }
    }
}
