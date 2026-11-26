pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 统计：近 7 天 / 近 30 天切换；完成率、连续天数、漏做次数用超大数字；趋势用 Canvas 柱状图。
// 下半页是喝水 / 睡眠 / 活动三条趋势（F1-6），数据从 Stub.trend 取，B1-1 接上后只改 Stub。
Item {
    id: page

    property Theme theme
    property int rangeDays: 7

    readonly property var chartData: page.rangeDays === 7 ? RecordVM.stats7 : RecordVM.stats30

    function hoursText(minutes) {
        var h = Math.round(minutes / 6) / 10
        return h === Math.floor(h) ? "" + h : h.toFixed(1)
    }

    // 三条趋势的展示配置：单位、柱顶数字怎么写、纵轴最大值、参考线。
    readonly property var trendKinds: [
        { kind: "water", title: "喝水", unit: "杯", floor: RecordVM.waterGoal, ref: RecordVM.waterGoal,
          note: "浅色横线是每天目标 " + RecordVM.waterGoal + " 杯" },
        { kind: "sleep", title: "睡眠", unit: "小时", floor: 480, ref: 0, note: "" },
        { kind: "activity", title: "活动", unit: "分钟", floor: 60, ref: 0, note: "" }
    ]

    function trendValueText(kind, v) {
        return kind === "sleep" ? page.hoursText(v) : "" + Math.round(v)
    }

    function trendSummary(cfg, series) {
        var today = series.length > 0 ? series[series.length - 1].value : 0
        var sum = 0
        var days = 0
        for (var i = 0; i < series.length; ++i) {
            if (series[i].value > 0) {
                sum += series[i].value
                ++days
            }
        }
        var text = "今天 " + page.trendValueText(cfg.kind, today) + " " + cfg.unit
        if (days > 1) {
            var avg = sum / days
            text += " · 有记录的 " + days + " 天平均 "
                + (cfg.kind === "sleep" ? page.hoursText(avg) : (Math.round(avg * 10) / 10)) + " " + cfg.unit
        }
        return text
    }

    signal back()

    readonly property int rate: Fmt.averageRate(page.chartData)
    readonly property int shownDays: page.chartData.length
    // 有提醒且全部做完的天数（替代原来“连续天数”：中间断一天就归零，太打击人）。
    readonly property int fullDays: {
        var n = 0
        for (var i = 0; i < page.chartData.length; ++i)
            if (page.chartData[i].total > 0 && page.chartData[i].done >= page.chartData[i].total)
                ++n
        return n
    }
    readonly property string cheer: page.rate >= 90 ? "做得很好！"
        : page.rate >= 60 ? "大部分都做到了"
        : "慢慢来，坚持就好"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        HeaderBar {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.topMargin: page.theme.gapSmall
            theme: page.theme
            title: "统计"
            // 真实数据只从开始使用那天算起，不足一个区间时如实写出天数。
            subtitle: page.chartData.length < page.rangeDays
                ? "已使用 " + page.chartData.length + " 天"
                : (page.rangeDays === 7 ? "最近 7 天" : "最近 30 天")
            showBack: true
            onBack: page.back()
        }

        Flickable {
            id: scroll
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
                bottomPadding: page.theme.gap

                Segmented {
                    objectName: "rangeSegmented"
                    width: list.width
                    theme: page.theme
                    options: ["最近 7 天", "最近 30 天"]
                    current: page.rangeDays === 7 ? "最近 7 天" : "最近 30 天"
                    onPicked: function (value) { page.rangeDays = value === "最近 7 天" ? 7 : 30 }
                }

                Rectangle {
                    width: list.width
                    height: emptyStats.implicitHeight + page.theme.gap * 2
                    radius: page.theme.radius
                    color: page.theme.surfaceAlt
                    visible: Fmt.sumTotal(page.chartData) === 0

                    AppText {
                        id: emptyStats
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.gap
                        theme: page.theme
                        role: "body"
                        color: page.theme.inkSoft
                        text: "还没有统计数据。先到「提醒」里添加一条，完成一次后会在这里显示。"
                        wrapMode: Text.WordWrap
                    }
                }

                // 主卡：环形完成率 + 一句鼓励的话 + 全做完的天数。
                Card {
                    objectName: "rateCard"
                    width: list.width
                    height: rateRow.implicitHeight + page.theme.px(20) * 2
                    theme: page.theme
                    visible: Fmt.sumTotal(page.chartData) > 0

                    RowLayout {
                        id: rateRow
                        x: page.theme.px(20)
                        y: page.theme.px(20)
                        width: parent.width - page.theme.px(20) * 2
                        spacing: page.theme.px(18)

                        Item {
                            Layout.preferredWidth: page.theme.px(100)
                            Layout.preferredHeight: page.theme.px(100)

                            Canvas {
                                id: ring
                                anchors.fill: parent
                                property real value: page.rate
                                onValueChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    var lw = width * 0.125
                                    var r = (width - lw) / 2
                                    ctx.lineWidth = lw
                                    ctx.lineCap = "round"
                                    ctx.strokeStyle = page.theme.primarySoft
                                    ctx.beginPath()
                                    ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2)
                                    ctx.stroke()
                                    if (value <= 0)
                                        return
                                    ctx.strokeStyle = page.theme.primary
                                    ctx.beginPath()
                                    ctx.arc(width / 2, height / 2, r, -Math.PI / 2,
                                            -Math.PI / 2 + Math.PI * 2 * Math.min(1, value / 100))
                                    ctx.stroke()
                                }
                            }

                            AppText {
                                anchors.centerIn: parent
                                theme: page.theme
                                role: "heading"
                                font.pixelSize: page.theme.px(26)
                                font.weight: Font.Black
                                color: page.theme.primaryInk
                                text: page.rate + "%"
                                wrapMode: Text.NoWrap
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: page.theme.px(4)

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: page.shownDays < page.rangeDays ? "这 " + page.shownDays + " 天完成率" : "完成率"
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "heading"
                                font.pixelSize: page.theme.px(26)
                                font.weight: Font.Black
                                text: page.cheer
                                wrapMode: Text.WordWrap
                            }

                        }
                    }
                }

                RowLayout {
                    width: list.width
                    spacing: page.theme.gapSmall
                    visible: Fmt.sumTotal(page.chartData) > 0

                    Card {
                        objectName: "fullDaysTile"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: fullCol.implicitHeight + page.theme.px(14) * 2
                        theme: page.theme

                        Column {
                            id: fullCol
                            x: page.theme.px(16)
                            y: page.theme.px(14)
                            width: parent.width - page.theme.px(32)

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: "全部做完"
                            }

                            AppText {
                                theme: page.theme
                                role: "hero"
                                font.pixelSize: page.theme.px(36)
                                font.weight: Font.Black
                                color: page.theme.primary
                                text: page.fullDays + " 天"
                                wrapMode: Text.NoWrap
                            }
                        }
                    }

                    Card {
                        objectName: "missedTile"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: missCol.implicitHeight + page.theme.px(14) * 2
                        theme: page.theme

                        Column {
                            id: missCol
                            x: page.theme.px(16)
                            y: page.theme.px(14)
                            width: parent.width - page.theme.px(32)

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: "漏做"
                            }

                            AppText {
                                theme: page.theme
                                role: "hero"
                                font.pixelSize: page.theme.px(36)
                                font.weight: Font.Black
                                color: page.theme.amber
                                text: Fmt.sumMissed(page.chartData) + " 次"
                                wrapMode: Text.NoWrap
                            }
                        }
                    }
                }

                Card {
                    width: list.width
                    height: chartColumn.implicitHeight + page.theme.px(18) * 2
                    theme: page.theme

                    Column {
                        id: chartColumn
                        width: parent.width - page.theme.px(18) * 2
                        x: page.theme.px(18)
                        y: page.theme.px(18)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "heading"
                            font.weight: Font.Bold
                            text: "每天做完多少"
                        }

                        BarChart {
                            objectName: "rateChart"
                            width: parent.width
                            height: page.theme.px(190)
                            theme: page.theme
                            values: page.chartData
                            showValues: false
                            twoTone: true
                            todayLabel: page.rangeDays === 7 ? "今天" : ""
                            refValue: 0
                        }

                        Flow {
                            width: parent.width
                            spacing: page.theme.px(16)

                            Row {
                                spacing: page.theme.px(6)

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: page.theme.px(14)
                                    height: width
                                    radius: page.theme.px(4)
                                    color: page.theme.primary
                                }

                                AppText {
                                    theme: page.theme
                                    role: "label"
                                    font.pixelSize: page.theme.px(19)
                                    color: page.theme.inkSoft
                                    text: "全部做完"
                                }
                            }

                            Row {
                                spacing: page.theme.px(6)

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: page.theme.px(14)
                                    height: width
                                    radius: page.theme.px(4)
                                    color: page.theme.amberBar
                                }

                                AppText {
                                    theme: page.theme
                                    role: "label"
                                    font.pixelSize: page.theme.px(19)
                                    color: page.theme.inkSoft
                                    text: "没全做完（柱子越高做得越多）"
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: summaryColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.primarySoft

                    Column {
                        id: summaryColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.primaryInk
                            text: Fmt.statsSummary(page.chartData)
                            wrapMode: Text.WordWrap
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "听语音播报"
                            iconName: "speaker"
                            kind: "secondary"
                            onClicked: DeviceVM.speak(Fmt.statsSummary(page.chartData), "manual-stats")
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "漏做只提示“已错过，请咨询医生或家人”，不会自动建议补服。"
                    wrapMode: Text.WordWrap
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "heading"
                    text: "生活记录趋势"
                }

                AppText {
                    width: list.width
                    visible: !Stub.trendsReal
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: DeviceVM.demoMode
                        ? "演示模式：过去几天是示例曲线，今天是真实记录。"
                        : "历史记录还在接入，目前只显示今天的数。"
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: page.trendKinds

                    delegate: Card {
                        id: trendCard
                        required property var modelData
                        readonly property var series: Stub.trend(modelData.kind, page.rangeDays)
                        readonly property real peak: {
                            var m = 0
                            for (var i = 0; i < series.length; ++i)
                                m = Math.max(m, series[i].value)
                            return m
                        }
                        readonly property bool empty: peak <= 0

                        objectName: "trend_" + modelData.kind
                        width: list.width
                        height: trendColumn.implicitHeight + page.theme.gap * 1.5
                        theme: page.theme

                        Column {
                            id: trendColumn
                            width: parent.width - page.theme.gap * 2
                            x: page.theme.gap
                            y: page.theme.px(12)
                            spacing: page.theme.gapSmall

                            RowLayout {
                                width: parent.width

                                AppText {
                                    theme: page.theme
                                    role: "body"
                                    font.weight: Font.DemiBold
                                    text: trendCard.modelData.title
                                }

                                Item { Layout.fillWidth: true }

                                AppText {
                                    theme: page.theme
                                    role: "label"
                                    color: page.theme.inkSoft
                                    text: "单位：" + trendCard.modelData.unit
                                }
                            }

                            AppText {
                                width: parent.width
                                visible: !trendCard.empty
                                theme: page.theme
                                role: "label"
                                color: page.theme.primaryInk
                                text: page.trendSummary(trendCard.modelData, trendCard.series)
                                wrapMode: Text.WordWrap
                            }

                            BarChart {
                                visible: !trendCard.empty
                                width: parent.width
                                height: page.theme.px(180)
                                theme: page.theme
                                values: trendCard.series
                                valueKey: "value"
                                // 留一成余量：目标线和最高的柱子不贴着图顶
                                maxValue: Math.max(trendCard.modelData.floor, trendCard.peak) * 1.1
                                refValue: trendCard.modelData.ref
                                formatValue: function (v) { return page.trendValueText(trendCard.modelData.kind, v) }
                            }

                            AppText {
                                width: parent.width
                                visible: !trendCard.empty && trendCard.modelData.note !== ""
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: trendCard.modelData.note
                                wrapMode: Text.WordWrap
                            }

                            AppText {
                                width: parent.width
                                visible: trendCard.empty
                                topPadding: page.theme.gapSmall
                                bottomPadding: page.theme.gapSmall
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                horizontalAlignment: Text.AlignHCenter
                                text: "暂无记录"
                            }
                        }
                    }
                }
            }
        }
    }
}
