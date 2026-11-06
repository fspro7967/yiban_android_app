pragma Singleton

import QtQuick

// 纯展示函数与时钟：不属于任何一个 VM，也不持有业务状态。
// 页面里所有 "现在几点 / 还有多久 / 完成率" 的格式化都走这里。
QtObject {
    id: fmt

    property date now: new Date()

    readonly property int nowMinutes: now.getHours() * 60 + now.getMinutes()
    readonly property string clockText: pad2(now.getHours()) + ":" + pad2(now.getMinutes())
    readonly property string dateText: (now.getMonth() + 1) + "月" + now.getDate() + "日 周"
        + "日一二三四五六".charAt(now.getDay())
    readonly property string greeting: {
        var h = now.getHours()
        if (h < 11)
            return "早上好"
        if (h < 13)
            return "中午好"
        if (h < 18)
            return "下午好"
        return "晚上好"
    }

    function pad2(n) {
        return (n < 10 ? "0" : "") + n
    }

    // 毫秒时间戳 → "HH:mm"
    function hhmm(ts) {
        if (!ts)
            return ""
        var d = new Date(Number(ts))
        return pad2(d.getHours()) + ":" + pad2(d.getMinutes())
    }

    function hhmmFromMinutes(minutes) {
        var m = ((Math.floor(minutes) % 1440) + 1440) % 1440
        return pad2(Math.floor(m / 60)) + ":" + pad2(m % 60)
    }

    function minutesOf(text) {
        var parts = String(text).split(":")
        if (parts.length < 2)
            return -1
        var h = parseInt(parts[0], 10)
        var m = parseInt(parts[1], 10)
        if (isNaN(h) || isNaN(m) || h < 0 || h > 23 || m < 0 || m > 59)
            return -1
        return h * 60 + m
    }

    function countdownText(minutes) {
        var m = Math.max(0, Math.round(minutes))
        if (m <= 0)
            return "现在"
        if (m < 60)
            return m + " 分钟后"
        var h = Math.floor(m / 60)
        var r = m % 60
        return h + " 小时后" + (r > 0 ? " " + r + " 分" : "")
    }

    function durationText(minutes) {
        var m = Math.max(0, Math.round(minutes))
        var h = Math.floor(m / 60)
        var r = m % 60
        if (h <= 0)
            return r + " 分钟"
        if (r === 0)
            return h + " 小时"
        return h + " 小时 " + r + " 分钟"
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
        for (var i = 0; list && i < list.length; ++i)
            n += list[i].done
        return n
    }

    function sumTotal(list) {
        var n = 0
        for (var i = 0; list && i < list.length; ++i)
            n += list[i].total
        return n
    }

    function sumMissed(list) {
        var n = 0
        for (var i = 0; list && i < list.length; ++i)
            n += Math.max(0, list[i].total - list[i].done)
        return n
    }

    function streak(list) {
        var n = 0
        for (var i = list ? list.length - 1 : -1; i >= 0; --i) {
            if (list[i].rate >= 100)
                ++n
            else
                break
        }
        return n
    }

    function statsSummary(list) {
        var days = list ? list.length : 0
        var missed = sumMissed(list)
        var rate = averageRate(list)
        if (missed === 0)
            return "最近 " + days + " 天完成率 " + rate + "%，一次都没漏。"
        return "最近 " + days + " 天完成率 " + rate + "%，有 " + missed + " 天漏了提醒。"
    }

    property Timer clock: Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: fmt.now = new Date()
    }
}
