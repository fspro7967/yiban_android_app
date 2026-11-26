pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 记一笔：喝水、睡眠、活动，全部是一键大按钮，记录后 5 秒内可撤销。
Item {
    id: page

    property Theme theme

    signal back()

    // 少记一杯：删掉今天最近的一条喝水记录（按多了能改），撤销条里可以加回来。
    function removeLastWater() {
        var list = RecordVM.records
        var d = Fmt.now
        var today0 = new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
        var best = null
        for (var i = 0; i < list.length; ++i) {
            var r = list[i]
            if (r.type !== "water" || Number(r.at) < today0)
                continue
            if (!best || Number(r.at) >= Number(best.at))
                best = r
        }
        if (!best)
            return
        if (RecordVM.removeRecord(best.recordId))
            Undo.show("已少记一杯，现在 " + RecordVM.waterCups + " 杯", function () { RecordVM.addWater() })
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
            title: "记一笔"
            subtitle: Fmt.dateText
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

                Card {
                    objectName: "waterCard"
                    width: list.width
                    height: waterColumn.implicitHeight + page.theme.px(18) * 2
                    theme: page.theme

                    Column {
                        id: waterColumn
                        width: parent.width - page.theme.px(18) * 2
                        x: page.theme.px(18)
                        y: page.theme.px(18)
                        spacing: page.theme.px(12)

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "heading"
                                font.weight: Font.Bold
                                text: "今天喝水"
                            }

                        }

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.px(8)

                            AppText {
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(52)
                                font.weight: Font.Black
                                color: page.theme.primaryInk
                                text: RecordVM.waterCups
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "heading"
                                color: page.theme.inkSoft
                                text: "/ " + RecordVM.waterGoal + " 杯"
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "label"
                                font.pixelSize: page.theme.px(18)
                                color: page.theme.inkSoft
                                horizontalAlignment: Text.AlignRight
                                text: "每杯 250 毫升"
                                wrapMode: Text.WordWrap
                            }
                        }

                        // 一杯一个杯子图标：喝了的填绿，没喝的只有轮廓。
                        Grid {
                            id: cupGrid
                            width: parent.width
                            columns: Math.min(RecordVM.waterGoal, 8)
                            spacing: page.theme.px(4)

                            Repeater {
                                model: RecordVM.waterGoal

                                delegate: Item {
                                    required property int index
                                    width: (cupGrid.width - cupGrid.spacing * (cupGrid.columns - 1)) / cupGrid.columns
                                    height: page.theme.px(40)

                                    Icon {
                                        anchors.centerIn: parent
                                        name: "cup"
                                        size: Math.min(parent.width, page.theme.px(40))
                                        strokeWidth: 1.8
                                        color: parent.index < RecordVM.waterCups ? page.theme.primary : "#C9BBA5"
                                        fill: parent.index < RecordVM.waterCups ? "#CFE6CB" : "transparent"
                                    }
                                }
                            }
                        }

                        // 两个键对半分；特大字号下并排放不下，改成上下排。
                        GridLayout {
                            width: parent.width
                            columns: page.theme.scaleLevel >= 2 ? 1 : 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            AppButton {
                                objectName: "addWater"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.button
                                theme: page.theme
                                text: RecordVM.waterCups >= RecordVM.waterGoal ? "今天喝够啦" : "喝了一杯"
                                kind: "primary"
                                enabled: RecordVM.waterCups < RecordVM.waterGoal
                                onClicked: {
                                    var id = RecordVM.addWater()
                                    if (id > 0)
                                        Undo.show("已记下：第 " + RecordVM.waterCups + " 杯水",
                                                  function () { RecordVM.removeRecord(id) })
                                }
                            }

                            AppButton {
                                objectName: "removeWater"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.button
                                theme: page.theme
                                text: "少记一杯"
                                kind: "secondary"
                                enabled: RecordVM.waterCups > 0
                                onClicked: page.removeLastWater()
                            }
                        }
                    }
                }

                Card {
                    objectName: "sleepCard"
                    width: list.width
                    height: sleepColumn.implicitHeight + page.theme.px(18) * 2
                    theme: page.theme

                    Column {
                        id: sleepColumn
                        width: parent.width - page.theme.px(18) * 2
                        x: page.theme.px(18)
                        y: page.theme.px(18)
                        spacing: page.theme.px(12)

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "heading"
                                font.weight: Font.Bold
                                text: "睡眠"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                visible: RecordVM.sleepStart !== "" && RecordVM.sleepEnd !== ""
                                theme: page.theme
                                role: "label"
                                font.pixelSize: page.theme.px(19)
                                color: page.theme.inkSoft
                                text: "最近一次"
                            }
                        }

                        // 时长：数字大、单位小，“7 小时 50 分”不再贴到右边。
                        RowLayout {
                            id: sleepRow
                            spacing: page.theme.px(4)
                            readonly property int mins: Math.max(0, RecordVM.sleepMinutes)

                            AppText {
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(46)
                                font.weight: Font.Black
                                color: page.theme.primaryInk
                                text: Math.floor(sleepRow.mins / 60)
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                Layout.alignment: Qt.AlignBaseline
                                Layout.rightMargin: page.theme.px(6)
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                text: "小时"
                            }

                            AppText {
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(46)
                                font.weight: Font.Black
                                color: page.theme.primaryInk
                                text: sleepRow.mins % 60
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                Layout.alignment: Qt.AlignBaseline
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                text: "分"
                            }
                        }

                        GridLayout {
                            width: parent.width
                            columns: 2
                            columnSpacing: page.theme.gapSmall

                            Repeater {
                                model: [
                                    { label: "入睡", value: RecordVM.sleepStart },
                                    { label: "起床", value: RecordVM.sleepEnd }
                                ]

                                delegate: Rectangle {
                                    id: timeBox
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: boxCol.implicitHeight + page.theme.px(10) * 2
                                    radius: page.theme.radiusSmall
                                    color: page.theme.surfaceAlt

                                    Column {
                                        id: boxCol
                                        x: page.theme.px(14)
                                        y: page.theme.px(10)
                                        width: parent.width - page.theme.px(28)

                                        AppText {
                                            theme: page.theme
                                            role: "label"
                                            font.pixelSize: page.theme.px(19)
                                            color: page.theme.inkSoft
                                            text: timeBox.modelData.label
                                        }

                                        AppText {
                                            theme: page.theme
                                            role: "heading"
                                            font.weight: Font.Bold
                                            color: timeBox.modelData.value ? page.theme.ink : page.theme.inkSoft
                                            text: timeBox.modelData.value ? timeBox.modelData.value : "未记录"
                                            wrapMode: Text.NoWrap
                                        }
                                    }
                                }
                            }
                        }

                        GridLayout {
                            width: parent.width
                            columns: page.theme.scaleLevel >= 2 ? 1 : 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.button
                                theme: page.theme
                                text: "我要睡了"
                                kind: "secondary"
                                onClicked: {
                                    var id = RecordVM.markSleepStart()
                                    Undo.show("已记下入睡时间", function () { RecordVM.removeRecord(id) })
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.button
                                theme: page.theme
                                text: "我起床了"
                                kind: "primary"
                                onClicked: {
                                    var id = RecordVM.markSleepEnd()
                                    Undo.show("已记下起床时间", function () { RecordVM.removeRecord(id) })
                                }
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            font.pixelSize: page.theme.px(19)
                            color: page.theme.inkSoft
                            text: "点一下就记下当前时间"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Card {
                    width: list.width
                    height: activityColumn.implicitHeight + page.theme.gap * 1.5
                    theme: page.theme

                    Column {
                        id: activityColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "heading"
                                text: "活动"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "heading"
                                color: page.theme.primary
                                text: RecordVM.activityMinutes + " 分钟"
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.inkSoft
                            text: "点一下，记下刚刚的活动。"
                        }

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.px(8)

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.buttonLarge
                                theme: page.theme
                                text: "散步"
                                subtitle: "15 分钟"
                                kind: "secondary"
                                onClicked: {
                                    var id = RecordVM.addActivity("散步")
                                    Undo.show("已记下散步", function () { RecordVM.removeRecord(id) })
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.buttonLarge
                                theme: page.theme
                                text: "家务"
                                subtitle: "10 分钟"
                                kind: "secondary"
                                onClicked: {
                                    var id = RecordVM.addActivity("家务")
                                    Undo.show("已记下家务", function () { RecordVM.removeRecord(id) })
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: page.theme.buttonLarge
                                theme: page.theme
                                text: "其他"
                                subtitle: "20 分钟"
                                kind: "secondary"
                                onClicked: {
                                    var id = RecordVM.addActivity("其他")
                                    Undo.show("已记下其他", function () { RecordVM.removeRecord(id) })
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: summaryColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radiusSmall
                    color: page.theme.primarySoft

                    Column {
                        id: summaryColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.px(4)

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.primaryInk
                            text: "今天的记录会用于本周统计。"
                        }

                    }
                }
            }
        }
    }
}
