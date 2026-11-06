pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 记一笔：喝水、睡眠、活动，全部是一键大按钮，记录后 5 秒内可撤销。
Item {
    id: page

    property Theme theme
    property MockBackend store

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
            subtitle: page.store.dateText
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
                                text: page.store.waterCups
                                wrapMode: Text.NoWrap
                            }

                            AppText {
                                theme: page.theme
                                role: "heading"
                                color: page.theme.inkSoft
                                text: "/ " + page.store.waterGoal + " 杯"
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Row {
                            width: parent.width
                            spacing: page.theme.px(6)

                            Repeater {
                                model: page.store.waterGoal

                                delegate: Rectangle {
                                    required property int index

                                    width: Math.floor((waterColumn.width - page.theme.px(6) * (page.store.waterGoal - 1)) / page.store.waterGoal)
                                    height: page.theme.px(34)
                                    radius: page.theme.px(6)
                                    color: index < page.store.waterCups ? page.theme.primary : page.theme.surfaceAlt
                                    border.width: 2
                                    border.color: index < page.store.waterCups ? page.theme.primary : page.theme.line
                                }
                            }
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.buttonLarge
                            theme: page.theme
                            text: page.store.waterCups >= page.store.waterGoal ? "今天已经喝够啦" : "＋ 我喝了一杯水"
                            kind: "primary"
                            enabled: page.store.waterCups < page.store.waterGoal
                            onClicked: page.store.addWater()
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
                                text: page.store.sleepDurationText()
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
                                text: "入睡 " + (page.store.sleepStart ? page.store.sleepStart : "未记录")
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                text: "起床 " + (page.store.sleepEnd ? page.store.sleepEnd : "未记录")
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
                                onClicked: page.store.markSleepStart()
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "起床"
                                kind: "primary"
                                onClicked: page.store.markSleepEnd()
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
                                text: page.store.activityMinutes + " 分钟"
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
                                onClicked: page.store.addActivity("散步")
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.buttonLarge
                                theme: page.theme
                                text: "家务"
                                subtitle: "10 分钟"
                                kind: "secondary"
                                onClicked: page.store.addActivity("家务")
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.buttonLarge
                                theme: page.theme
                                text: "其他"
                                subtitle: "20 分钟"
                                kind: "secondary"
                                onClicked: page.store.addActivity("其他")
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
