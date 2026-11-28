pragma Singleton

import QtQuick

// 还没接上 VM 的数据先放这里（F1-6 的"假数据阶段"），页面只认这一个出口，
// 接口合进来后只改本文件，页面不用动：
//   * trend()         ← B1-1 统计趋势接口（I4，RecordVM 的趋势序列）
//   * paused / pause  ← B1-2 暂停模式（I5，落库 + 暂停期间不弹不升级）
//   * speechRate      ← B2-7 离线播报语速
// 每一项的 xxxReal 为 false 时，页面要标明"接入中"，不能让看护人以为已经生效。
QtObject {
    id: stub

    // ---------------------------------------------------------------- 趋势（B1-1）

    readonly property bool trendsReal: false

    // 过去几天的演示曲线（只在演示模式下用）；今天永远取 RecordVM 的真实值。
    readonly property var demoWater: [6, 7, 5, 8, 6, 4, 7, 8, 5, 6, 7]
    readonly property var demoSleep: [420, 390, 450, 360, 480, 405, 435, 400, 465]
    readonly property var demoActivity: [30, 45, 20, 60, 35, 0, 40, 25, 50]

    // kind: "water"（杯）/ "sleep"（分钟）/ "activity"（分钟）。
    // 返回从旧到新 days 项 {date, value, label, isToday}，按自然日补齐 0，最后一项是今天，
    // 形状和 B1-1 约定的 {date, value} 一致，多出来的 label/isToday 给 BarChart 用。
    function trend(kind, days) {
        var today = new Date(Fmt.now.getFullYear(), Fmt.now.getMonth(), Fmt.now.getDate())
        var demo = DeviceVM.demoMode
        var todayValue = kind === "water" ? RecordVM.waterCups
            : kind === "sleep" ? RecordVM.sleepMinutes
            : RecordVM.activityMinutes
        var pattern = kind === "water" ? demoWater : kind === "sleep" ? demoSleep : demoActivity

        var out = []
        for (var i = days - 1; i >= 0; --i) {
            var d = new Date(today.getFullYear(), today.getMonth(), today.getDate() - i)
            var value = 0
            if (i === 0)
                value = todayValue
            else if (demo)
                value = pattern[(d.getDate() + d.getMonth()) % pattern.length]
            var showLabel = days <= 7 || i === days - 1 || i === 0 || i % 5 === 0
            out.push({
                date: d.getFullYear() + "-" + Fmt.pad2(d.getMonth() + 1) + "-" + Fmt.pad2(d.getDate()),
                value: value,
                label: showLabel ? (days <= 7 ? "日一二三四五六".charAt(d.getDay())
                                              : (d.getMonth() + 1) + "/" + d.getDate())
                                 : "",
                isToday: i === 0
            })
        }
        return out
    }

    // ---------------------------------------------------------------- 暂停（B1-2）

    readonly property bool pauseReal: false

    property bool paused: false
    property double pausedUntilMs: 0

    function pauseUntil(untilMs) {
        stub.pausedUntilMs = untilMs
        stub.paused = untilMs > Date.now()
    }

    function resume() {
        stub.paused = false
        stub.pausedUntilMs = 0
    }

    // 到期自动恢复（真接口由 B1 的调度负责，这里只让界面状态跟着走）。
    property Timer pauseExpiry: Timer {
        interval: 1000
        repeat: true
        running: stub.paused
        onTriggered: {
            if (Date.now() >= stub.pausedUntilMs)
                stub.resume()
        }
    }

    // ---------------------------------------------------------------- 语速（B2-7）
    // 11/27 已接真接口：DeviceVM.speechRate 持久化 + Android 系统 TTS 实时变速。
    readonly property bool speechRateReal: true

    // 1.0 = 系统正常语速；默认 0.9，三档 0.75/0.9/1.1。只读展示，写入走 DeviceVM.setSpeechRate。
    readonly property real speechRate: DeviceVM.speechRate
}
