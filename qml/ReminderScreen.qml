pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 全屏到点提醒：事项名最大（要做什么比几点更重要），时间退为小一号的辅助信息；
// 巨大确认键、10 分钟后备键。升级链（I2）由 AppCore 推进，这里只读等级和倒计时：
// 1 级 = 已提醒，2 级 = 加强播报（2 分钟），3 级 = 已通知家人（plan.escalateAfterMin，默认 15 分钟）。
// 三级时的拨号 / 短信归 X-1 决定的那一方，QML 不调 DeviceVM.sos(3)，否则会拨两次。
Item {
    id: screen

    property Theme theme
    property bool active: false
    property var occ: null

    readonly property string caregiverLabel: ProfileVM.caregiverName.length > 0 ? ProfileVM.caregiverName : "家人"

    // 升级链状态：每次 occurrence 变化（等级变化、today 刷新）时从 ReminderVM 重新取一份，
    // 倒计时只在本地按 ReminderVM.nowMs 往下走，不自己计时，也不自己触发升级。
    property int level: 1
    property double deadlineMs: 0      // 下一级的时刻；-1 表示不在升级链上（已升到 3 级）
    property double levelStartMs: 0    // 当前这一级开始的时刻，用来算进度条
    property double notifyFamilyMs: 0  // 升到 3 级（通知家人）的时刻
    // 接口冻结清单 A.2：提醒弹出 2 分钟升 2 级。只用来画 2 级那段进度条的起点，
    // 倒计时本身以 nextEscalationInSec 为准。
    readonly property int voiceBoostSec: 120

    readonly property bool escalated: level >= 3
    // 不在升级链上但还没升到 3 级：比如已经过了有效期（missed，允许补做），不再倒计时。
    readonly property bool inChain: !escalated && deadlineMs > 0
    readonly property double nowMs: ReminderVM.nowMs
    // 显示用的截止时刻：对齐到 AppCore 的 tick 相位（apply 时的 nowMs 就是某次 tick 的时刻）。
    // 直接拿精确截止时刻取整，跳变点可能正好落在 tick 上（重启后 tick 相位是随机的），
    // 几毫秒的抖动就让数字一次跳 2（真机 11/26 看到 112→110）。对齐后每次 tick 稳定减 1，误差不超过半秒。
    property double shownDeadlineMs: 0
    property double shownFamilyMs: 0
    readonly property int secondsLeft: shownDeadlineMs > 0 ? Math.max(0, Math.round((shownDeadlineMs - nowMs) / 1000)) : 0
    readonly property int familyLeft: shownFamilyMs > 0 ? Math.max(0, Math.round((shownFamilyMs - nowMs) / 1000)) : 0

    function alignToTick(ts, now) {
        return ts > 0 ? now + Math.round((ts - now) / 1000) * 1000 : ts
    }
    readonly property real levelProgress: {
        if (escalated || deadlineMs <= levelStartMs)
            return 1
        return Math.max(0, Math.min(1, (nowMs - levelStartMs) / (deadlineMs - levelStartMs)))
    }

    readonly property int snoozeLeft: occ ? occ.snoozeRemaining : 3

    anchors.fill: parent
    visible: active
    z: 50

    signal closed()

    function spokenText(item) {
        return item.title + "。" + (item.dose || "")
    }

    function open(item) {
        occ = item
        active = true
        refresh()
        if (item)
            DeviceVM.speak((level >= 2 ? "再次提醒您：" : "提醒您：") + spokenText(item), "reminder")
    }

    function close() {
        active = false
        closed()
    }

    // 从 ReminderVM.today 里取这一条的最新状态（today 每次读都是现算的，带 nextEscalationInSec）。
    function refresh() {
        if (!occ)
            return
        var list = ReminderVM.today
        for (var i = 0; i < list.length; ++i) {
            if (list[i].occurrenceId === occ.occurrenceId) {
                apply(list[i])
                return
            }
        }
        apply(occ)
    }

    function apply(item) {
        occ = item
        var lv = item.state === "escalated" ? 3 : Math.max(1, item.escalationLevel || 1)
        var now = ReminderVM.nowMs
        var notifiedAt = Number(item.notifiedAtTs || 0)
        var escalateMs = Math.max(1, item.escalateAfterMin || 15) * 60 * 1000
        var inSec = item.nextEscalationInSec !== undefined ? item.nextEscalationInSec : -1

        level = lv
        // 有 notifiedAtTs 时按 A.2 的规则直接算出精确时刻；nextEscalationInSec 是向上取整的秒数，
        // 拿它反推会让截止时刻每次刷新都偏 0–1 秒。没有基线时才退回用它。
        if (lv >= 3 || inSec < 0)
            deadlineMs = -1
        else if (notifiedAt > 0)
            deadlineMs = notifiedAt + (lv <= 1 ? Math.min(voiceBoostSec * 1000, escalateMs) : escalateMs)
        else
            deadlineMs = now + inSec * 1000
        if (notifiedAt <= 0)
            notifiedAt = lv <= 1 && deadlineMs > 0 ? deadlineMs - voiceBoostSec * 1000 : now
        notifyFamilyMs = lv < 3 ? notifiedAt + escalateMs : -1
        levelStartMs = lv <= 1 ? notifiedAt : notifiedAt + voiceBoostSec * 1000
        if (levelStartMs >= deadlineMs)
            levelStartMs = now
        shownDeadlineMs = alignToTick(deadlineMs, now)
        shownFamilyMs = alignToTick(notifyFamilyMs, now)
    }

    // "1 分 05 秒" / "14 分钟"：一分钟以上只报整分钟，老人不用盯着秒数跳。
    function leftText(sec) {
        if (sec <= 0)
            return "马上"
        if (sec < 60)
            return sec + " 秒后"
        if (sec < 120)
            return "1 分 " + Fmt.pad2(sec - 60) + " 秒后"
        return Math.ceil(sec / 60) + " 分钟后"
    }

    function confirm() {
        if (!occ)
            return
        ReminderVM.confirm(occ.occurrenceId)
        close()
    }

    function snooze() {
        if (!occ)
            return
        ReminderVM.snooze(occ.occurrenceId)
        close()
    }

    property Connections escalationWatcher: Connections {
        target: ReminderVM
        enabled: screen.active && screen.occ !== null

        function onEscalationChanged(occurrenceId, level) {
            if (occurrenceId !== screen.occ.occurrenceId)
                return
            var before = screen.level
            screen.refresh()
            // 升到 2 级：加强播报，再念一遍（B2 的播报接口）。
            if (level === 2 && before < 2)
                DeviceVM.speak("再次提醒您：" + screen.spokenText(screen.occ), "reminder-escalate")
        }
    }

    // 提醒开着的时候这一条变成"错过且不允许补做"（有效期走完、时间被往后调）：
    // 完成键已经不会生效，留着全屏页只会把人卡住，直接收起并说明原因。
    property Connections lateWatcher: Connections {
        target: ReminderVM
        enabled: screen.active && screen.occ !== null

        function onTodayChanged() {
            var list = ReminderVM.today
            for (var i = 0; i < list.length; ++i) {
                var item = list[i]
                if (item.occurrenceId !== screen.occ.occurrenceId)
                    continue
                if (item.state === "missed" && !item.canConfirm) {
                    var title = item.title
                    screen.close()
                    Notify.info("「" + title + "」已过时，按要求不能补做")
                    return
                }
                screen.apply(item)
                return
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: screen.theme.reminderBgTop }
            GradientStop { position: 1.0; color: screen.theme.reminderBgBottom }
        }
    }

    Rectangle {
        id: glow
        anchors.fill: parent
        color: "transparent"
        border.width: screen.theme.px(8)
        border.color: screen.theme.reminderAccent
        opacity: 0.6
    }

    SequentialAnimation {
        running: screen.active
        loops: Animation.Infinite
        NumberAnimation { target: glow; property: "opacity"; from: 0.75; to: 0.2; duration: 900 }
        NumberAnimation { target: glow; property: "opacity"; from: 0.2; to: 0.75; duration: 900 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: screen.theme.pageMargin
        spacing: screen.theme.gap

        // 顶栏不能把页面撑宽：RowLayout 里没有 fillWidth 的子项是刚性的，两个固定宽度加起来
        // 超过屏宽时，整列（倒计时、完成键、推迟键）都会跟着伸出屏幕右边。
        // 所以标签和按钮都允许收窄，标签字放不下时自动缩小。
        RowLayout {
            Layout.fillWidth: true
            spacing: screen.theme.gapSmall

            Rectangle {
                Layout.fillWidth: true
                Layout.minimumWidth: screen.theme.px(60)
                Layout.preferredWidth: chipText.implicitWidth + screen.theme.px(26)
                Layout.maximumWidth: chipText.implicitWidth + screen.theme.px(26)
                Layout.preferredHeight: screen.theme.px(50)
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.14)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.3)

                AppText {
                    id: chipText
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - screen.theme.px(26))
                    theme: screen.theme
                    role: "label"
                    color: screen.theme.reminderInk
                    // 两种文案一样长：夜间静音时直接换字，不加长标签，也不多占一行。
                    text: DeviceVM.nightQuietActive ? "夜间静音" : "到点提醒"
                    wrapMode: Text.NoWrap
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: screen.theme.px(14)
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                Layout.fillWidth: true
                Layout.minimumWidth: Math.min(implicitWidth, screen.theme.px(168))
                Layout.preferredWidth: screen.theme.px(168)
                Layout.maximumWidth: screen.theme.px(168)
                Layout.preferredHeight: screen.theme.touch
                theme: screen.theme
                text: "再听一遍"
                kind: "outlineLight"
                onClicked: {
                    if (screen.occ)
                        DeviceVM.speak("提醒您：" + screen.occ.title + "。" + (screen.occ.dose || ""), "manual-reminder")
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: info.height
            clip: true

            Column {
                id: info
                width: parent.width
                spacing: screen.theme.px(6)

                AppText {
                    theme: screen.theme
                    role: "heading"
                    color: screen.theme.reminderAccent
                    text: screen.occ ? Fmt.hhmm(screen.occ.dueTs) : ""
                    wrapMode: Text.NoWrap
                }

                AppText {
                    width: parent.width
                    theme: screen.theme
                    role: "hero"
                    color: screen.theme.reminderInk
                    text: screen.occ ? screen.occ.title : ""
                    wrapMode: Text.WordWrap
                }

                AppText {
                    width: parent.width
                    visible: text !== ""
                    theme: screen.theme
                    role: "heading"
                    color: screen.theme.reminderInk
                    opacity: 0.95
                    text: screen.occ ? (screen.occ.dose || "") : ""
                }

                AppText {
                    width: parent.width
                    visible: text !== ""
                    theme: screen.theme
                    role: "body"
                    color: screen.theme.reminderInk
                    opacity: 0.8
                    text: screen.occ ? (screen.occ.note || "") : ""
                }
            }
        }

        // 升级横幅：1 级普通提示（到加强播报的倒计时），2 级换成醒目的琥珀底并闪烁边框，
        // 3 级红底"已通知家人"。三级各自一套颜色，老人不用读字也能看出事情在变严重。
        Rectangle {
            id: escalationBanner
            objectName: "escalationBanner"
            Layout.fillWidth: true
            Layout.preferredHeight: escalationColumn.implicitHeight + screen.theme.px(24)
            radius: screen.theme.radiusSmall
            color: screen.level >= 3 ? screen.theme.sosRed
                : screen.level === 2 ? screen.theme.reminderAccent
                : Qt.rgba(0, 0, 0, 0.26)
            // 动画只改 pulseWidth，不直接改 border.width：否则绑定被覆盖，升到 3 级后边框会停在半途。
            property real pulseWidth: screen.theme.px(4)
            border.width: screen.level === 2 ? pulseWidth : 1
            border.color: screen.level >= 3 ? "#FFFFFF"
                : screen.level === 2 ? "#FFFFFF"
                : Qt.rgba(1, 1, 1, 0.16)

            readonly property color ink: screen.level === 2 ? screen.theme.reminderBgBottom : screen.theme.reminderInk

            SequentialAnimation on pulseWidth {
                running: screen.active && screen.level === 2
                loops: Animation.Infinite
                NumberAnimation { to: screen.theme.px(1); duration: 500 }
                NumberAnimation { to: screen.theme.px(5); duration: 500 }
            }

            Column {
                id: escalationColumn
                width: parent.width - screen.theme.px(28)
                anchors.centerIn: parent
                spacing: screen.theme.px(8)

                AppText {
                    objectName: "escalationTitle"
                    width: parent.width
                    theme: screen.theme
                    role: screen.level >= 2 ? "heading" : "body"
                    font.weight: screen.level >= 2 ? Font.Bold : Font.Normal
                    color: escalationBanner.ink
                    text: screen.level >= 3 ? "已通知 " + screen.caregiverLabel + "，请尽快确认"
                        : !screen.inChain ? "已经过了时间，请尽快完成"
                        : screen.level === 2 ? "⚠ 还没确认，" + screen.leftText(screen.secondsLeft) + "通知 " + screen.caregiverLabel
                        : screen.leftText(screen.secondsLeft) + "再提醒一遍"
                    wrapMode: Text.WordWrap
                }

                AppText {
                    width: parent.width
                    visible: screen.inChain && screen.level === 1 && screen.notifyFamilyMs > 0
                    theme: screen.theme
                    role: "label"
                    color: escalationBanner.ink
                    opacity: 0.85
                    text: Fmt.countdownText(Math.ceil(screen.familyLeft / 60)) + "还没确认就通知 " + screen.caregiverLabel
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    visible: screen.inChain
                    height: screen.theme.px(8)
                    radius: height / 2
                    color: screen.level === 2 ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(1, 1, 1, 0.18)

                    Rectangle {
                        width: parent.width * screen.levelProgress
                        height: parent.height
                        radius: height / 2
                        color: screen.level === 2 ? screen.theme.reminderBgBottom : screen.theme.reminderAccent
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: screen.theme.gapSmall

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: screen.theme.reminderConfirm
                theme: screen.theme
                text: screen.occ && screen.occ.title.indexOf("药") >= 0 ? "✓ 我已吃好" : "✓ 我已经完成"
                kind: "light"
                onClicked: screen.confirm()
            }

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: screen.theme.buttonLarge
                theme: screen.theme
                text: "稍后 10 分钟"
                kind: "outlineLight"
                enabled: !screen.escalated && screen.snoozeLeft > 0
                onClicked: screen.snooze()
            }

            AppText {
                Layout.fillWidth: true
                theme: screen.theme
                role: "label"
                color: screen.theme.reminderInk
                opacity: 0.75
                horizontalAlignment: Text.AlignHCenter
                text: screen.escalated ? "已通知家人，请尽快确认"
                    : screen.snoozeLeft > 0 ? "还可以推迟 " + screen.snoozeLeft + " 次"
                    : "已推迟 3 次，请先吃药或联系家人"
            }
        }
    }
}
