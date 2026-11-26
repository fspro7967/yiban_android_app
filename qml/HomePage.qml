pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 首页：一张“现在该做”的主卡（能直接点完成）+ 今天进度 + 一排四个入口 + 底部呼救。
// 主卡永远是全屏最显眼的东西；同步之类老人用不上的状态退到一条小胶囊里。
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

    // 到点 / 过时：今天这条已经到了（或推迟到的时刻已经过了）。
    readonly property bool due: hasToday && next.dueNow === true
    readonly property bool snoozedAhead: hasToday && next.state === "snoozed"
        && Number(next.snoozeUntilTs || 0) > ReminderVM.nowMs
    // 过了多少分钟：从推迟到的时刻（有的话）或原定时刻算起，跟着 nowMs 实时走。
    readonly property int overdueMinutes: {
        if (!due)
            return 0
        var base = Number(next.snoozeUntilTs || 0) > 0 ? Number(next.snoozeUntilTs) : Number(next.dueTs)
        return Math.max(0, Math.floor((ReminderVM.nowMs - base) / 60000))
    }
    readonly property bool canConfirm: hasToday && next.canConfirm === true
    readonly property bool canSnooze: hasToday && next.canSnooze === true && next.state !== "escalated"
    // 主卡上的“我已完成”只在该做的时候出现；还没到点的提醒不在首页催着点。
    readonly property bool showActions: canConfirm && (due || next.state === "snoozed")

    // 今天每条提醒一段：做完 / 错过 / 还没做。
    readonly property var segments: {
        var list = ReminderVM.today
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var st = list[i].state
            out.push(st === "confirmed" ? "done" : st === "missed" ? "missed" : "todo")
        }
        return out
    }
    function countOf(kind) {
        var n = 0
        for (var i = 0; i < segments.length; ++i)
            if (segments[i] === kind)
                ++n
        return n
    }

    // 只在手机上提示：电脑版没有权限模型。口径和权限页一样（Fmt.missingPermissions）。
    readonly property int missingPermissions: Qt.platform.os === "android"
        ? Fmt.missingPermissions(DeviceVM.permissionDiagnostics) : 0

    // 首页顶部的小胶囊：图标 + 一句话。clickable 时是按钮（触控区往外扩到够大）。
    component StatusPill: Rectangle {
        id: pill
        property string iconName: ""
        property string text: ""
        // ok（绿）/ warn（琥珀）/ neutral（灰）/ action（白底绿字描边，表示能点）
        property string tone: "neutral"
        property bool clickable: tone === "action" || tone === "warn"
        // 文字后面的小圆角标（待上传条数），0 不显示。
        property int badge: 0
        signal clicked()

        readonly property color ink: tone === "ok" ? page.theme.primaryInk
            : tone === "warn" ? page.theme.amberInk
            : tone === "action" ? page.theme.primary
            : page.theme.inkSoft

        height: page.theme.px(44)
        width: pillRow.implicitWidth + page.theme.px(24)
        radius: height / 2
        color: tone === "ok" ? page.theme.primarySoft
            : tone === "warn" ? page.theme.amberSoft
            : tone === "action" ? page.theme.surface
            : page.theme.surfaceAlt
        border.width: tone === "action" ? 2 : 0
        border.color: page.theme.line
        scale: pillMouse.pressed ? 0.97 : 1

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: page.theme.px(6)

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: pill.iconName
                size: page.theme.px(20)
                strokeWidth: 2.4
                color: pill.ink
            }

            AppText {
                anchors.verticalCenter: parent.verticalCenter
                theme: page.theme
                role: "label"
                font.pixelSize: page.theme.px(19)
                font.weight: Font.Bold
                color: pill.ink
                text: pill.text
                wrapMode: Text.NoWrap
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: pill.badge > 0
                height: page.theme.px(24)
                width: Math.max(height, badgeText.implicitWidth + page.theme.px(10))
                radius: height / 2
                color: pill.ink

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    color: "#FFFFFF"
                    font.family: page.theme.fontFamily
                    font.pixelSize: page.theme.px(16)
                    font.weight: Font.Bold
                    text: pill.badge > 99 ? "99+" : pill.badge
                }
            }
        }

        MouseArea {
            id: pillMouse
            anchors.fill: parent
            anchors.margins: -page.theme.px(4)
            enabled: pill.clickable
            property real lastClick: 0
            onClicked: {
                var t = Date.now()
                if (Fmt.tooSoon(lastClick, t, 400))
                    return
                lastClick = t
                pill.clicked()
            }
        }

        Accessible.role: clickable ? Accessible.Button : Accessible.StaticText
    }

    function confirmNext() {
        if (!page.hasToday)
            return
        var id = page.next.occurrenceId
        var title = page.next.title
        ReminderVM.confirm(id)
        // 首页一点就完成，按错的代价比在提醒页大：同样给 5 秒撤销。
        Undo.show("已完成：" + title, function () { ReminderVM.undo(id) })
    }

    function snoozeNext() {
        if (page.hasToday)
            ReminderVM.snooze(page.next.occurrenceId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.topMargin: page.theme.px(12)
            spacing: page.theme.px(4)

            AppText {
                id: greetingText
                Layout.fillWidth: true
                theme: page.theme
                role: "title"
                font.weight: Font.Bold
                // 一行放不下时在「，」后换行，别把名字截断或拆开
                readonly property string oneLine: ProfileVM.elderName.length > 0
                    ? Fmt.greeting + "，" + ProfileVM.elderName
                    : "你好"
                text: greetingMetrics.advanceWidth <= width || ProfileVM.elderName.length === 0
                    ? oneLine
                    : Fmt.greeting + "，\n" + ProfileVM.elderName
                wrapMode: Text.NoWrap
                maximumLineCount: 2
                elide: Text.ElideRight

                TextMetrics {
                    id: greetingMetrics
                    font: greetingText.font
                    text: greetingText.oneLine
                }
            }

            AppText {
                Layout.fillWidth: true
                theme: page.theme
                role: "label"
                color: page.theme.inkSoft
                text: Fmt.dateText + "  " + Fmt.clockText
                wrapMode: Text.WordWrap
            }

            // 状态小条：外设板连没连上、手动同步。放不下时自动折到第二行。
            Flow {
                Layout.fillWidth: true
                spacing: page.theme.gapSmall

                // 外设板（雷达 + 灯板 + 确认键）状态，只读；绑定、重连在管理后台里做。
                StatusPill {
                    id: devicePill
                    objectName: "homeDeviceStatus"
                    readonly property string link: BoardVM.linkState
                    tone: BoardVM.ready ? "ok"
                        : link === "unsupported" ? "warn"
                        : "neutral"
                    iconName: BoardVM.ready ? "check" : link === "unsupported" ? "alert" : "cloudOff"
                    text: BoardVM.ready ? "设备已连接"
                        : link === "unsupported" ? "设备异常"
                        : BoardVM.bound && (link === "connecting" || link === "discovering") ? "设备连接中"
                        : BoardVM.bound ? "设备未连接"
                        : "设备未绑定"
                    Accessible.name: text
                }

                // 手动同步：和管理页「立即同步」同一入口，任何时候都在；
                // 没令牌时 SyncVM 会弹 toast 说明原因。带上待上传条数，出错时变琥珀色。
                StatusPill {
                    id: syncButton
                    objectName: "homeSyncButton"
                    readonly property bool failed: SyncVM.syncState === "error"
                    readonly property bool syncing: SyncVM.syncState === "syncing"
                    clickable: !syncing
                    tone: failed ? "warn" : "action"
                    opacity: syncing ? 0.7 : 1
                    iconName: failed ? "alert" : "refresh"
                    text: syncing ? "正在同步" : failed ? "同步出错" : "同步"
                    badge: syncing || failed ? 0 : SyncVM.pendingCount
                    Accessible.name: syncing ? "正在同步"
                        : failed ? "同步出错，点一下重试"
                        : SyncVM.pendingCount > 0 ? "同步，" + SyncVM.pendingCount + " 条待上传"
                        : "立即同步"
                    onClicked: SyncVM.retrySync()
                }
            }
        }

        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: page.theme.px(8)
            contentWidth: width
            contentHeight: body.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: body
                x: page.theme.pageMargin
                width: scroll.width - page.theme.pageMargin * 2
                spacing: page.theme.px(10)
                topPadding: page.theme.px(2)
                bottomPadding: page.theme.px(12)

                // 暂停中要一眼看出来：老人以为"今天怎么不响了"时，这条就是答案。
                NoticeBar {
                    objectName: "pauseNotice"
                    width: body.width
                    visible: Stub.paused
                    theme: page.theme
                    iconName: "pause"
                    message: "提醒已暂停到 " + Fmt.dayHourText(Stub.pausedUntilMs)
                        + (Stub.pauseReal ? "" : "（预览，提醒仍会照常响）")
                }

                NoticeBar {
                    objectName: "permissionNotice"
                    width: body.width
                    visible: page.missingPermissions > 0
                    theme: page.theme
                    message: "有 " + page.missingPermissions + " 项权限没开，提醒可能不响"
                    actionText: "去检查"
                    onActionClicked: page.openPage("permissions")
                }

                // 主卡：该做的事。到点后变琥珀底，直接放“我已完成 / 稍后”。
                Card {
                    id: hero
                    objectName: "homeHero"
                    width: body.width
                    height: heroBody.implicitHeight + page.theme.px(16) * 2
                    theme: page.theme
                    radius: page.theme.radius + page.theme.px(4)
                    color: !page.hasNext ? page.theme.primarySoft
                        : page.due ? page.theme.amberSoft
                        : page.theme.surface
                    border.color: !page.hasNext ? "#CFE3CA"
                        : page.due ? page.theme.amberBar
                        : page.theme.cardLine

                    Column {
                        id: heroBody
                        width: parent.width - page.theme.px(14) * 2
                        x: page.theme.px(14)
                        y: page.theme.px(16)
                        spacing: page.theme.px(8)

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            AppText {
                                Layout.fillWidth: true
                                theme: page.theme
                                role: "label"
                                font.weight: Font.Bold
                                color: page.due ? page.theme.amberInk : page.theme.inkSoft
                                text: !page.hasNext ? (page.hasPlans ? "今天" : "开始使用")
                                    : page.due ? "现在该做"
                                    : page.snoozedAhead ? "已推迟到"
                                    : page.hasToday ? "下一条提醒"
                                    : (page.next.dayLabel || "下一条提醒")
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                objectName: "heroChip"
                                // 一条提醒都还没有时不显示（“✓ 0 条提醒”读起来像完成了什么）。
                                visible: page.hasNext || page.hasPlans
                                Layout.preferredHeight: chipRow.implicitHeight + page.theme.px(8)
                                Layout.preferredWidth: chipRow.implicitWidth + page.theme.px(22)
                                radius: height / 2
                                color: !page.hasNext ? page.theme.primary
                                    : page.due ? page.theme.amber
                                    : page.theme.surfaceAlt

                                Row {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: page.theme.px(5)

                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: !page.hasNext ? "check" : "clock"
                                        size: page.theme.px(18)
                                        strokeWidth: 2.6
                                        color: chipText.color
                                    }

                                    AppText {
                                        id: chipText
                                        anchors.verticalCenter: parent.verticalCenter
                                        theme: page.theme
                                        role: "label"
                                        font.pixelSize: page.theme.px(18)
                                        font.weight: Font.Bold
                                        color: !page.hasNext || page.due ? "#FFFFFF" : page.theme.inkSoft
                                        wrapMode: Text.NoWrap
                                        text: {
                                            if (!page.hasNext)
                                                return page.hasPlans ? "全部完成" : "0 条提醒"
                                            if (page.due)
                                                return Fmt.agoText(page.overdueMinutes)
                                            // 一律用目标时间戳和实时 nowMs 现算：current 里的 diffMinutes
                                            // 只在状态变化时刷新，时间走了它不会跟着变。
                                            var target = page.next.dueTs
                                            if (page.snoozedAhead)
                                                target = page.next.snoozeUntilTs
                                            var m = Math.max(0, Math.ceil((target - ReminderVM.nowMs) / 60000))
                                            return Fmt.countdownText(m)
                                        }
                                    }
                                }
                            }
                        }

                        // 有提醒：时间 + 事项名在一行；没有提醒：图标 + 一句话。
                        RowLayout {
                            width: parent.width
                            spacing: page.theme.px(14)

                            AppText {
                                visible: page.hasNext
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "heading"
                                font.weight: Font.Bold
                                color: page.due ? page.theme.amberInk : page.theme.primaryInk
                                text: page.hasNext && page.next.displayTime !== undefined ? page.next.displayTime : ""
                                wrapMode: Text.NoWrap
                            }

                            Rectangle {
                                visible: !page.hasNext
                                Layout.preferredWidth: page.theme.px(52)
                                Layout.preferredHeight: page.theme.px(52)
                                radius: width / 2
                                color: page.theme.surface

                                Icon {
                                    anchors.centerIn: parent
                                    name: page.hasPlans ? "check" : "plus"
                                    size: page.theme.px(30)
                                    strokeWidth: 3
                                    color: page.theme.primary
                                }
                            }

                            AppText {
                                Layout.fillWidth: true
                                Layout.alignment: page.hasNext ? Qt.AlignBaseline : Qt.AlignVCenter
                                theme: page.theme
                                role: page.hasNext ? "hero" : "heading"
                                font.pixelSize: page.hasNext ? page.theme.px(40) : page.theme.fontHeading
                                font.weight: Font.Bold
                                text: page.hasNext && page.next.title !== undefined ? page.next.title
                                    : (page.hasPlans ? "今天的提醒都完成啦" : "还没有提醒")
                                wrapMode: Text.WordWrap
                            }
                        }

                        AppText {
                            width: parent.width
                            visible: text !== ""
                            theme: page.theme
                            role: "body"
                            font.pixelSize: page.theme.px(22)
                            color: page.theme.inkSoft
                            text: page.hasNext && page.next ? (page.next.dose || "")
                                : (page.hasPlans
                                    ? "记得喝水、活动一下，晚上再测一次血压"
                                    : "点下面「提醒」，再点「新建」，添加第一条吃药或喝水提醒。")
                            wrapMode: Text.WordWrap
                        }

                        Item {
                            width: parent.width
                            height: page.theme.px(2)
                            visible: page.showActions
                        }

                        // 特大字号下两个键并排放不下“我已完成”，改成上下排。
                        GridLayout {
                            width: parent.width
                            visible: page.showActions
                            columns: page.theme.scaleLevel >= 2 ? 1 : 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            AppButton {
                                objectName: "heroConfirm"
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.button
                                theme: page.theme
                                text: page.hasToday && page.next.state === "missed" ? "补做完成" : "我已完成"
                                iconName: "check"
                                kind: "primary"
                                onClicked: page.confirmNext()
                            }

                            AppButton {
                                objectName: "heroSnooze"
                                visible: page.canSnooze
                                Layout.fillWidth: page.theme.scaleLevel >= 2
                                Layout.preferredWidth: page.theme.px(108)
                                Layout.preferredHeight: page.theme.button
                                theme: page.theme
                                text: "稍后"
                                subtitle: "10 分钟"
                                kind: "secondary"
                                onClicked: page.snoozeNext()
                            }
                        }
                    }
                }

                // 今天进度：每条提醒一段，绿 = 做完，红 = 错过，琥珀 = 还没做。
                Card {
                    objectName: "todayProgress"
                    width: body.width
                    height: progressBody.implicitHeight + page.theme.px(12) * 2
                    theme: page.theme
                    visible: page.hasPlans && ReminderVM.todayTotal > 0

                    Column {
                        id: progressBody
                        width: parent.width - page.theme.px(20) * 2
                        x: page.theme.px(20)
                        y: page.theme.px(12)
                        spacing: page.theme.px(8)

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
                                font.weight: Font.Bold
                                color: page.theme.primary
                                text: ReminderVM.todayDone + " / " + ReminderVM.todayTotal
                            }
                        }

                        Row {
                            id: segRow
                            width: parent.width
                            spacing: page.theme.px(6)

                            Repeater {
                                model: page.segments

                                delegate: Rectangle {
                                    required property string modelData
                                    width: (segRow.width - segRow.spacing * (page.segments.length - 1)) / Math.max(1, page.segments.length)
                                    height: page.theme.px(12)
                                    radius: height / 2
                                    color: modelData === "done" ? page.theme.primary
                                        : modelData === "missed" ? page.theme.danger
                                        : page.theme.amberBar
                                }
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: page.theme.px(16)

                            Repeater {
                                model: [
                                    { kind: "done", label: "做完", color: page.theme.primary },
                                    { kind: "missed", label: "错过", color: page.theme.danger },
                                    { kind: "todo", label: "还没做", color: page.theme.amberBar }
                                ]

                                delegate: Row {
                                    id: legendItem
                                    required property var modelData
                                    readonly property int n: page.countOf(modelData.kind)
                                    visible: n > 0
                                    spacing: page.theme.px(6)

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: page.theme.px(12)
                                        height: width
                                        radius: page.theme.px(3)
                                        color: legendItem.modelData.color
                                    }

                                    AppText {
                                        theme: page.theme
                                        role: "label"
                                        font.pixelSize: page.theme.px(18)
                                        color: page.theme.inkSoft
                                        text: legendItem.modelData.label + " " + legendItem.n
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    width: body.width
                    spacing: page.theme.px(10)

                    NavTile {
                        objectName: "navReminders"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: page.theme.px(92)
                        theme: page.theme
                        iconName: "bell"
                        title: "提醒"
                        subtitle: ReminderVM.pendingCount + " 条待办"
                        tint: page.theme.tileAmber
                        badgeColor: page.theme.amber
                        onClicked: page.openPage("reminders")
                    }

                    NavTile {
                        objectName: "navRecord"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: page.theme.px(92)
                        theme: page.theme
                        iconName: "pencil"
                        title: "记录"
                        subtitle: "喝水、睡眠"
                        tint: page.theme.tileGreen
                        badgeColor: page.theme.primary
                        onClicked: page.openPage("record")
                    }

                    NavTile {
                        objectName: "navStats"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: page.theme.px(92)
                        theme: page.theme
                        iconName: "chart"
                        title: "统计"
                        subtitle: "完成率 " + Fmt.averageRate(RecordVM.stats7) + "%"
                        tint: page.theme.tileBlue
                        badgeColor: page.theme.blue
                        onClicked: page.openPage("stats")
                    }

                    NavTile {
                        objectName: "navSettings"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: page.theme.px(92)
                        theme: page.theme
                        iconName: "sliders"
                        title: "设置"
                        subtitle: "字号、语音"
                        tint: page.theme.tileSand
                        badgeColor: page.theme.inkSoft
                        onClicked: page.openPage("settings")
                    }
                }


                NoticeBar {
                    objectName: "offlineNotice"
                    width: body.width
                    visible: DeviceVM.networkStatus === "offline"
                    theme: page.theme
                    iconName: "cloudOff"
                    message: "手机没有联网。提醒照常响，记录先存在手机里，联网后再上传。"
                }
            }
        }

        SosButton {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.bottomMargin: page.theme.gap
            Layout.topMargin: page.theme.px(4)
            Layout.preferredHeight: page.theme.px(84)
            theme: page.theme
            onActivated: page.sosActivated()
        }
    }
}
