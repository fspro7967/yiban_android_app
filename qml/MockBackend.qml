pragma ComponentBehavior: Bound

import QtQuick
import QtTextToSpeech

// 演示用内存后端。
// 真实项目里这一层应替换为 C++ QObject（网络 + 契约里的状态机），QML 只做绑定。
// 这里刻意保留“动作 -> 新数组 -> 界面刷新”的模式，和契约“客户端只提交动作、状态由后端改”一致。
QtObject {
    id: backend

    // 演示开关：关闭后不会自动弹提醒，方便截图/调试
    property bool autoReminders: true
    property bool voiceEnabled: true
    property bool nightQuiet: true

    property string elderName: "陈奶奶"
    property string caregiverName: "女儿 陈晓"
    property string caregiverPhone: "138 0000 5678"

    property date now: new Date()
    property int nextId: 5

    // 4 条今日提醒，覆盖“已确认 / 待确认 / 待提醒”
    property var reminders: [
        { "id": "r1", "time": "07:00", "title": "晨起喝水", "dose": "温水 1 杯", "note": "小口慢慢喝", "repeat": "每天", "state": "confirmed", "snoozeUsed": 0, "snoozeUntil": 0 },
        { "id": "r2", "time": "08:00", "title": "吃降压药", "dose": "苯磺酸氨氯地平 1 片", "note": "早饭后，温水送服", "repeat": "每天", "state": "confirmed", "snoozeUsed": 0, "snoozeUntil": 0 },
        { "id": "r3", "time": "12:00", "title": "吃降糖药", "dose": "二甲双胍 1 片", "note": "随午餐服用", "repeat": "每天", "state": "pending", "snoozeUsed": 0, "snoozeUntil": 0 },
        { "id": "r4", "time": "18:00", "title": "测血压", "dose": "坐稳后测量，记录数值", "note": "测量前安静休息 5 分钟", "repeat": "每天", "state": "pending", "snoozeUsed": 0, "snoozeUntil": 0 }
    ]

    property int waterCups: 3
    property int waterGoal: 8
    property int waterCupMl: 250
    property string sleepStart: "22:40"
    property string sleepEnd: "06:30"
    property int sleepMinutes: 470
    property int activityMinutes: 25

    property var stats7: []
    property var stats30: []

    signal toast(string text)
    signal reminderDue(var occurrence)

    property TextToSpeech tts: TextToSpeech {
        locale: Qt.locale("zh_CN")
        rate: -0.15
    }

    property Timer clock: Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: backend.tick()
    }

    property Timer undoTimer: Timer {
        interval: 1000
        repeat: true
        running: backend.undoVisible
        onTriggered: {
            if (!backend.undoVisible)
                return
            backend.undoSecondsLeft -= 1
            if (backend.undoSecondsLeft <= 0)
                backend.undoVisible = false
        }
    }

    property bool undoVisible: false
    property string undoText: ""
    property int undoSecondsLeft: 0
    property var undoCallback: null

    Component.onCompleted: {
        stats7 = makeStats(7)
        stats30 = makeStats(30)
    }

    // ---------- 时间与展示 ----------

    function pad2(n) {
        return n < 10 ? "0" + n : "" + n
    }

    function hhmm(d) {
        return pad2(d.getHours()) + ":" + pad2(d.getMinutes())
    }

    function hhmmFromMs(ms) {
        return hhmm(new Date(ms))
    }

    function minutesOf(timeText) {
        var parts = ("" + timeText).split(":")
        return parseInt(parts[0], 10) * 60 + parseInt(parts[1], 10)
    }

    function durationText(minutes) {
        var m = Math.max(0, Math.round(minutes))
        var h = Math.floor(m / 60)
        var rest = m % 60
        if (h <= 0)
            return rest + " 分钟"
        if (rest === 0)
            return h + " 小时"
        return h + " 小时 " + rest + " 分钟"
    }

    function countdownText(seconds) {
        var s = Math.max(0, Math.round(seconds))
        var m = Math.floor(s / 60)
        var rest = s % 60
        if (m <= 0)
            return rest + " 秒"
        return m + " 分 " + rest + " 秒"
    }

    readonly property string clockText: hhmm(now)
    readonly property string dateText: (now.getMonth() + 1) + "月" + now.getDate() + "日 周" + weekdayShort[now.getDay()]

    readonly property var weekdayLong: ["星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"]
    readonly property var weekdayShort: ["日", "一", "二", "三", "四", "五", "六"]

    readonly property string greeting: now.getHours() < 6 ? "夜深了"
        : now.getHours() < 11 ? "早上好"
        : now.getHours() < 14 ? "中午好"
        : now.getHours() < 18 ? "下午好"
        : "晚上好"

    readonly property int nowMinutes: now.getHours() * 60 + now.getMinutes()

    // ---------- 今日提醒 ----------

    readonly property int todayDone: countState("confirmed")
    readonly property int todayTotal: reminders.length
    readonly property int pendingCount: countNotConfirmed()

    readonly property var sortedReminders: {
        var list = reminders.slice(0)
        list.sort(function(a, b) { return minutesOf(a.time) - minutesOf(b.time) })
        return list
    }

    readonly property var nextReminder: computeNext()

    function countState(value) {
        var n = 0
        for (var i = 0; i < reminders.length; ++i) {
            if (reminders[i].state === value)
                n += 1
        }
        return n
    }

    function countNotConfirmed() {
        var n = 0
        for (var i = 0; i < reminders.length; ++i) {
            if (reminders[i].state !== "confirmed")
                n += 1
        }
        return n
    }

    function getReminder(id) {
        for (var i = 0; i < reminders.length; ++i) {
            if (reminders[i].id === id)
                return reminders[i]
        }
        return null
    }

    function cloneReminder(r) {
        return {
            "id": r.id,
            "time": r.time,
            "title": r.title,
            "dose": r.dose,
            "note": r.note,
            "repeat": r.repeat,
            "state": r.state,
            "snoozeUsed": r.snoozeUsed,
            "snoozeUntil": r.snoozeUntil
        }
    }

    function computeNext() {
        var cur = nowMinutes
        var best = null
        for (var i = 0; i < reminders.length; ++i) {
            var r = reminders[i]
            if (r.state === "confirmed" || r.state === "missed")
                continue

            var diff = 0
            var display = r.time
            if (r.state === "snoozed" && r.snoozeUntil > now.getTime()) {
                diff = Math.ceil((r.snoozeUntil - now.getTime()) / 60000)
                display = hhmmFromMs(r.snoozeUntil)
            } else if (r.state === "notified" || r.state === "escalated" || r.state === "snoozed") {
                diff = 0
            } else {
                diff = Math.max(0, minutesOf(r.time) - cur)
            }

            if (best === null || diff < best.diffMinutes) {
                best = cloneReminder(r)
                best.diffMinutes = diff
                best.displayTime = display
                best.dueNow = diff === 0
            }
        }
        return best
    }

    function snoozeTimeText(r) {
        return r.snoozeUntil > 0 ? hhmmFromMs(r.snoozeUntil) : r.time
    }

    function setState(id, value) {
        var out = []
        for (var i = 0; i < reminders.length; ++i) {
            var copy = cloneReminder(reminders[i])
            if (copy.id === id)
                copy.state = value
            out.push(copy)
        }
        reminders = out
    }

    // 每秒检查一次：到点 / 推迟到点，只触发一次
    function tick() {
        now = new Date()
        if (!autoReminders)
            return

        var cur = nowMinutes
        for (var i = 0; i < reminders.length; ++i) {
            var r = reminders[i]
            if (r.state === "pending" && minutesOf(r.time) <= cur) {
                fireReminder(r.id)
                return
            }
            if (r.state === "snoozed" && r.snoozeUntil > 0 && r.snoozeUntil <= now.getTime()) {
                fireReminder(r.id)
                return
            }
        }
    }

    function fireReminder(id) {
        var r = getReminder(id)
        if (!r)
            return
        setState(id, "notified")
        var copy = cloneReminder(getReminder(id))
        reminderDue(copy)
    }

    function triggerDemoReminder() {
        var r = null
        for (var i = 0; i < reminders.length; ++i) {
            if (reminders[i].state === "pending" || reminders[i].state === "notified") {
                r = reminders[i]
                break
            }
        }
        if (!r)
            r = reminders[0]
        if (!r)
            return
        setState(r.id, "notified")
        reminderDue(cloneReminder(getReminder(r.id)))
    }

    // ---------- 提醒动作 ----------

    function confirmOccurrence(id) {
        var r = getReminder(id)
        if (!r) {
            toast("已记录")
            return
        }
        setState(id, "confirmed")
        beginUndo("已记录：" + r.title, function() {
            setState(id, "notified")
        })
        toast("已记下：" + r.time + " " + r.title)
    }

    function snoozeOccurrence(id, minutes) {
        var r = getReminder(id)
        if (!r)
            return false
        if (r.snoozeUsed >= 3) {
            toast("已经推迟 3 次了，请先吃药或联系家人")
            return false
        }
        var used = r.snoozeUsed + 1
        var until = now.getTime() + minutes * 60000
        var out = []
        for (var i = 0; i < reminders.length; ++i) {
            var copy = cloneReminder(reminders[i])
            if (copy.id === id) {
                copy.state = "snoozed"
                copy.snoozeUsed = used
                copy.snoozeUntil = until
            }
            out.push(copy)
        }
        reminders = out
        toast("好的，" + minutes + " 分钟后再提醒你")
        return true
    }

    function notifyCaregiver(occ) {
        toast("已通知 " + caregiverName + "：" + occ.title + " 还没有确认")
        if (occ && occ.id)
            setState(occ.id, "escalated")
    }

    function addReminder(title, time, dose, note, repeatKind) {
        var id = "r" + nextId
        nextId += 1
        var item = {
            "id": id,
            "time": time,
            "title": title,
            "dose": dose,
            "note": note,
            "repeat": repeatKind,
            "state": "pending",
            "snoozeUsed": 0,
            "snoozeUntil": 0
        }
        reminders = reminders.concat([item])
        toast("已添加提醒：" + time + " " + title)
    }

    function removeReminder(id) {
        var out = []
        for (var i = 0; i < reminders.length; ++i) {
            if (reminders[i].id !== id)
                out.push(reminders[i])
        }
        reminders = out
        toast("已删除这条提醒")
    }

    // ---------- 记录 ----------

    function addWater() {
        if (waterCups >= waterGoal) {
            toast("今天的水已经喝够了")
            return
        }
        waterCups = waterCups + 1
        beginUndo("已记录 1 杯水", function() {
            waterCups = Math.max(0, waterCups - 1)
        })
        toast("今天第 " + waterCups + " 杯水，慢慢喝")
    }

    function markSleepStart() {
        var oldStart = sleepStart
        var oldEnd = sleepEnd
        var oldMinutes = sleepMinutes
        sleepStart = clockText
        sleepEnd = ""
        sleepMinutes = 0
        beginUndo("已记录入睡时间 " + sleepStart, function() {
            sleepStart = oldStart
            sleepEnd = oldEnd
            sleepMinutes = oldMinutes
        })
        toast("晚安，已记下入睡时间")
    }

    function markSleepEnd() {
        if (sleepStart === "") {
            toast("请先点「我现在睡了」")
            return
        }
        var oldStart = sleepStart
        var oldEnd = sleepEnd
        var oldMinutes = sleepMinutes
        var end = clockText
        var mins = minutesOf(end) - minutesOf(sleepStart)
        if (mins <= 0)
            mins += 24 * 60
        sleepEnd = end
        sleepMinutes = mins
        beginUndo("已记录起床时间 " + end, function() {
            sleepStart = oldStart
            sleepEnd = oldEnd
            sleepMinutes = oldMinutes
        })
        toast("已记下：睡了 " + durationText(mins))
    }

    function sleepDurationText() {
        return sleepMinutes > 0 ? durationText(sleepMinutes) : "还没有记录"
    }

    function addActivity(kind) {
        var minutes = kind === "散步" ? 15 : (kind === "家务" ? 10 : 20)
        activityMinutes += minutes
        beginUndo("已记录" + kind + " " + minutes + " 分钟", function() {
            activityMinutes = Math.max(0, activityMinutes - minutes)
        })
        toast("已记录：" + kind + " " + minutes + " 分钟")
    }

    // ---------- 统计 ----------

    function makeStats(days) {
        var base = days === 7 ? [50, 50, 100, 100, 100, 100, 100]
                              : [50, 100, 75, 100, 100, 50, 100, 100, 75, 100]
        var out = []
        for (var i = days - 1; i >= 0; --i) {
            var d = new Date(now.getFullYear(), now.getMonth(), now.getDate() - i)
            var idx = (days - 1 - i) % base.length
            var rate = base[idx]
            var total = 4
            var done = Math.round(rate * total / 100)
            var showLabel = days === 7 || i === days - 1 || i === 0 || i % 5 === 0
            out.push({
                "label": showLabel ? (days === 7 ? weekdayShort[d.getDay()] : (d.getMonth() + 1) + "/" + d.getDate()) : "",
                "rate": rate,
                "done": done,
                "total": total,
                "isToday": i === 0
            })
        }
        return out
    }

    function averageRate(list) {
        if (!list || list.length === 0)
            return 0
        var sum = 0
        for (var i = 0; i < list.length; ++i)
            sum += list[i].rate
        return Math.round(sum / list.length)
    }

    function sumDone(list) {
        var n = 0
        for (var i = 0; i < list.length; ++i)
            n += list[i].done
        return n
    }

    function sumTotal(list) {
        var n = 0
        for (var i = 0; i < list.length; ++i)
            n += list[i].total
        return n
    }

    function sumMissed(list) {
        return Math.max(0, sumTotal(list) - sumDone(list))
    }

    function streak(list) {
        var n = 0
        for (var i = list.length - 1; i >= 0; --i) {
            if (list[i].rate >= 100)
                n += 1
            else
                break
        }
        return n
    }

    function statsSummary(days) {
        var list = days === 7 ? stats7 : stats30
        return "过去 " + days + " 天，提醒完成率 " + averageRate(list) + "%，连续 " + streak(list)
                + " 天全部完成，漏做 " + sumMissed(list) + " 次。"
    }

    // ---------- 语音 / 撤销 / 重置 ----------

    function speak(text) {
        if (!voiceEnabled || !text)
            return
        if (nightQuiet && (now.getHours() >= 22 || now.getHours() < 7))
            return
        tts.stop()
        tts.say(text)
    }

    function beginUndo(text, callback) {
        undoText = text
        undoCallback = callback
        undoSecondsLeft = 5
        undoVisible = true
    }

    function undo() {
        var callback = undoCallback
        undoVisible = false
        undoText = ""
        undoCallback = null
        undoSecondsLeft = 0
        if (callback)
            callback()
        toast("已撤销")
    }

    function resetDemo() {
        reminders = [
            { "id": "r1", "time": "07:00", "title": "晨起喝水", "dose": "温水 1 杯", "note": "小口慢慢喝", "repeat": "每天", "state": "confirmed", "snoozeUsed": 0, "snoozeUntil": 0 },
            { "id": "r2", "time": "08:00", "title": "吃降压药", "dose": "苯磺酸氨氯地平 1 片", "note": "早饭后，温水送服", "repeat": "每天", "state": "confirmed", "snoozeUsed": 0, "snoozeUntil": 0 },
            { "id": "r3", "time": "12:00", "title": "吃降糖药", "dose": "二甲双胍 1 片", "note": "随午餐服用", "repeat": "每天", "state": "pending", "snoozeUsed": 0, "snoozeUntil": 0 },
            { "id": "r4", "time": "18:00", "title": "测血压", "dose": "坐稳后测量，记录数值", "note": "测量前安静休息 5 分钟", "repeat": "每天", "state": "pending", "snoozeUsed": 0, "snoozeUntil": 0 }
        ]
        nextId = 5
        waterCups = 3
        sleepStart = "22:40"
        sleepEnd = "06:30"
        sleepMinutes = 470
        activityMinutes = 25
        undoVisible = false
        stats7 = makeStats(7)
        stats30 = makeStats(30)
        toast("演示数据已恢复")
    }
}
