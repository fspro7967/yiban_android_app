pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 记一笔：喝水、睡眠、活动，全部是一键大按钮，记录后 5 秒内可撤销。
Item {
    id: page

    property Theme theme

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

                Rectangle {
                    width: list.width
                    height: waterColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: waterColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "heading"
                                text: "喝水"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: page.theme.inkSoft
                                text: "每杯约 250 毫升"
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.px(6)

                            AppText {
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(56)
                                color: page.theme.primaryInk
                                text: RecordVM.waterCups
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                theme: page.theme
                                role: "heading"
                                color: page.theme.inkSoft
                                text: "/ " + RecordVM.waterGoal + " 杯"
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Row {
                            width: parent.width
                            spacing: page.theme.px(6)

                            Repeater {
                                model: RecordVM.waterGoal

                                delegate: Rectangle {
                                    required property int index

                                    width: Math.floor((waterColumn.width - page.theme.px(6) * (RecordVM.waterGoal - 1)) / RecordVM.waterGoal)
                                    height: page.theme.px(34)
                                    radius: page.theme.px(6)
                                    color: index < RecordVM.waterCups ? page.theme.primary : page.theme.surfaceAlt
                                    border.width: 2
                                    border.color: index < RecordVM.waterCups ? page.theme.primary : page.theme.line
                                }
                            }
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.buttonLarge
                            theme: page.theme
                            text: RecordVM.waterCups >= RecordVM.waterGoal ? "今天已经喝够啦" : "＋ 我喝了一杯水"
                            kind: "primary"
                            enabled: RecordVM.waterCups < RecordVM.waterGoal
                            onClicked: {
                                var id = RecordVM.addWater()
                                if (id > 0)
                                    Undo.show("已记下：第 " + RecordVM.waterCups + " 杯水",
                                              function () { RecordVM.removeRecord(id) })
                            }
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: sleepColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: sleepColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "heading"
                            text: "睡眠"
                        }

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.scaleLevel === 2 ? page.theme.px(32) : page.theme.px(46)
                                color: page.theme.primaryInk
                                text: Fmt.durationText(RecordVM.sleepMinutes)
                                wrapMode: Text.NoWrap
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Column {
                            width: parent.width
                            spacing: page.theme.px(2)

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                text: "入睡 " + (RecordVM.sleepStart ? RecordVM.sleepStart : "未记录")
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                text: "起床 " + (RecordVM.sleepEnd ? RecordVM.sleepEnd : "未记录")
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "我睡了"
                                kind: "secondary"
                                onClicked: {
                                    var id = RecordVM.markSleepStart()
                                    Undo.show("已记下入睡时间", function () { RecordVM.removeRecord(id) })
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "起床"
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
                            color: page.theme.inkSoft
                            text: "点一下记录当前时间，不用手动输入。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: activityColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

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
                            spacing: page.theme.gapSmall

                            AppButton {
                                Layout.fillWidth: true
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

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.primaryInk
                            opacity: 0.8
                            text: "演示版只保存在本机，不会上传。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
