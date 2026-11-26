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

                RowLayout {
                    width: list.width
                    spacing: page.theme.gapSmall

                    AppButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.touch
                        theme: page.theme
                        text: "7 天"
                        kind: "secondary"
                        selected: page.rangeDays === 7
                        onClicked: page.rangeDays = 7
                    }

                    AppButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.touch
                        theme: page.theme
                        text: "30 天"
                        kind: "secondary"
                        selected: page.rangeDays === 30
                        onClicked: page.rangeDays = 30
                    }
                }

                Rectangle {
                    width: list.width
                    height: emptyStats.implicitHeight + page.theme.gap * 2
                    radius: page.theme.radius
                    color: page.theme.surfaceAlt
                    border.width: 2
                    border.color: page.theme.line
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

                RowLayout {
                    width: list.width
                    spacing: page.theme.gap

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.px(156)
                        radius: page.theme.radius
                        color: page.theme.surface
                        border.width: 2
                        border.color: page.theme.line

                        Column {
                            anchors.centerIn: parent
                            spacing: page.theme.px(4)

                            AppText {
                                width: page.theme.px(164)
                                theme: page.theme
                                role: "hero"
                                color: page.theme.primary
                                horizontalAlignment: Text.AlignHCenter
                                text: Fmt.averageRate(page.chartData) + "%"
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                width: page.theme.px(164)
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                horizontalAlignment: Text.AlignHCenter
                                text: "完成率"
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.theme.px(156)
                        radius: page.theme.radius
                        color: page.theme.surface
                        border.width: 2
                        border.color: page.theme.line

                        Column {
                            anchors.centerIn: parent
                            spacing: page.theme.px(4)

                            AppText {
                                width: page.theme.px(164)
                                theme: page.theme
                                role: "hero"
                                color: page.theme.primary
                                horizontalAlignment: Text.AlignHCenter
                                text: Fmt.streak(page.chartData)
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                width: page.theme.px(164)
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                horizontalAlignment: Text.AlignHCenter
                                text: "连续天数"
                            }
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: page.theme.px(96)
                    radius: page.theme.radius
                    color: page.theme.amberSoft
                    border.width: 2
                    border.color: page.theme.amber

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: page.theme.gap
                        anchors.rightMargin: page.theme.gap

                        AppText {
                            theme: page.theme
                            role: "body"
                            color: page.theme.amberInk
                            text: "漏做次数"
                        }

                        Item { Layout.fillWidth: true }

                        AppText {
                            theme: page.theme
                            role: "heading"
                            color: page.theme.amberInk
                            text: Fmt.sumMissed(page.chartData) + " 次"
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: chartColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: chartColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "每天完成率"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: "深绿 = 今天"
                            }
                        }

                        BarChart {
                            width: parent.width
                            height: page.theme.px(210)
                            theme: page.theme
                            values: page.chartData
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: page.rangeDays === 7
                                ? "每一根柱子代表一天，数字是当天完成率。"
                                : "每 5 天标一次日期，柱子的高度是当天完成率。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: summaryColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.primarySoft
                    border.width: 1
                    border.color: page.theme.primary

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

                    delegate: Rectangle {
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
                        radius: page.theme.radius
                        color: page.theme.surface
                        border.width: 2
                        border.color: page.theme.line

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
