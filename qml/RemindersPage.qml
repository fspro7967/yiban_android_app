pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 今日提醒清单：每条提醒一张大卡；确认/推迟都只走显式大按钮，点卡片本身不执行动作。
Item {
    id: page

    property Theme theme

    signal back()
    signal addRequested()

    function confirmItem(occurrenceId) {
        ReminderVM.confirm(occurrenceId)
    }

    function snoozeItem(occurrenceId) {
        ReminderVM.snooze(occurrenceId)
    }

    // 撤销 = 回到"已提醒未确认"；撤销条里再点一次就重新确认。
    function reopenItem(occurrenceId) {
        ReminderVM.undo(occurrenceId)
        Undo.show("已恢复为待确认", function () {
            ReminderVM.confirm(occurrenceId)
        })
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
            title: "提醒"
            subtitle: ReminderVM.todayTotal > 0
                ? ReminderVM.todayDone + " / " + ReminderVM.todayTotal
                : "还没有提醒"
            showBack: true
            actionText: "新建"
            onBack: page.back()
            onAction: page.addRequested()
        }

        Flickable {
            id: scroll
            objectName: "remindersScroll"
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
                    height: introColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radiusSmall
                    color: page.theme.primarySoft
                    border.width: 1
                    border.color: page.theme.primary

                    Column {
                        id: introColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.px(4)

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.primaryInk
                            text: "到点会全屏提醒，确认后才算完成；按错了可以马上撤销。"
                            wrapMode: Text.WordWrap
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.primaryInk
                            opacity: 0.8
                            text: "每条提醒最多推迟 3 次，每次 10 分钟。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: nextHint.implicitHeight + page.theme.gap
                    radius: page.theme.radiusSmall
                    color: page.theme.surfaceAlt
                    border.width: 1
                    border.color: page.theme.line
                    visible: ReminderVM.upcoming.length > 0

                    Column {
                        id: nextHint
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(8)
                        spacing: page.theme.px(2)

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: ReminderVM.upcoming.length > 0
                                ? "接下来 · " + ReminderVM.upcoming[0].dayLabel + "还有 " + ReminderVM.upcoming.length + " 条"
                                : ""
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            text: ReminderVM.upcoming.length > 0
                                ? Fmt.hhmm(ReminderVM.upcoming[0].dueTs) + "  " + ReminderVM.upcoming[0].title
                                : ""
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                        }
                    }
                }

                Repeater {
                    model: ReminderVM.today

                    delegate: Rectangle {
                        id: card
                        required property var modelData

                        // notified 也要可操作：确认后撤销会回到 notified，若不算 actionable，两个按钮都会变灰。
                        readonly property bool actionable: modelData.state === "pending"
                            || modelData.state === "notified"
                            || modelData.state === "snoozed"
                        readonly property bool isDone: modelData.state === "confirmed"
                        readonly property bool isMissed: modelData.state === "missed"
                        readonly property bool isEscalated: modelData.state === "escalated"
                        // 能不能点完成由 core 决定：错过后只有"允许过时补做"的计划才能补点。
                        readonly property bool canComplete: modelData.canConfirm === true
                        readonly property bool lateMakeup: isMissed && canComplete
                        readonly property bool strict: modelData.allowLate === false

                        width: list.width
                        height: cardColumn.implicitHeight + page.theme.gap * 1.5
                        radius: page.theme.radius
                        color: page.theme.surface
                        border.width: 2
                        border.color: card.actionable ? page.theme.amber : page.theme.line

                        Column {
                            id: cardColumn
                            width: parent.width - page.theme.gap * 2
                            x: page.theme.gap
                            y: page.theme.px(12)
                            spacing: page.theme.gapSmall

                            RowLayout {
                                width: parent.width
                                spacing: page.theme.gapSmall

                                AppText {
                                    theme: page.theme
                                    role: "heading"
                                    font.pixelSize: page.theme.px(34)
                                    color: page.theme.ink
                                    text: Fmt.hhmm(card.modelData.dueTs)
                                }

                                StateChip {
                                    theme: page.theme
                                    status: card.modelData.state
                                }

                                Item { Layout.fillWidth: true }

                                Item {
                                    Layout.preferredWidth: page.theme.px(48)
                                    Layout.preferredHeight: page.theme.touch

                                    AppText {
                                        anchors.centerIn: parent
                                        theme: page.theme
                                        role: "label"
                                        color: page.theme.primary
                                        text: "管理"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: manageSheet.openFor(card.modelData)
                                    }
                                }
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "heading"
                                text: card.modelData.title
                                wrapMode: Text.WordWrap
                            }

                            AppText {
                                width: parent.width
                                visible: text !== ""
                                theme: page.theme
                                role: "body"
                                color: page.theme.inkSoft
                                text: {
                                    var parts = []
                                    if (card.modelData.dose)
                                        parts.push(card.modelData.dose)
                                    if (card.modelData.note)
                                        parts.push(card.modelData.note)
                                    return parts.join(" · ")
                                }
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                width: parent.width
                                spacing: page.theme.gapSmall

                                AppButton {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.button
                                    theme: page.theme
                                    text: card.isDone ? "已完成"
                                        : card.lateMakeup ? "✓ 补做"
                                        : card.isMissed ? "已错过"
                                        : "✓ 完成"
                                    subtitle: card.lateMakeup ? "已过时" : ""
                                    kind: "primary"
                                    enabled: card.canComplete
                                    onClicked: page.confirmItem(card.modelData.occurrenceId)
                                }

                                AppButton {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.button
                                    theme: page.theme
                                    text: card.isDone ? "撤销"
                                        : card.isEscalated ? "已通知家人"
                                        : card.isMissed ? "已错过"
                                        : "推迟"
                                    subtitle: card.isDone || card.isEscalated || card.isMissed ? "" : "10 分钟"
                                    kind: card.isDone ? "ghost" : "secondary"
                                    enabled: card.isDone || card.actionable
                                    onClicked: {
                                        if (card.isDone)
                                            page.reopenItem(card.modelData.occurrenceId)
                                        else if (card.actionable)
                                            page.snoozeItem(card.modelData.occurrenceId)
                                    }
                                }
                            }

                            AppText {
                                width: parent.width
                                visible: card.strict && !card.isDone
                                theme: page.theme
                                role: "label"
                                color: card.isMissed ? page.theme.danger : page.theme.inkSoft
                                text: card.isMissed
                                    ? "已过时，这条提醒要求按时完成，不能补做"
                                    : "⏱ 需按时完成，错过后不能补做"
                                wrapMode: Text.WordWrap
                            }

                            AppText {
                                width: parent.width
                                visible: card.actionable && card.modelData.snoozeCount >= 3
                                theme: page.theme
                                role: "label"
                                color: page.theme.danger
                                text: "已推迟 3 次，请先完成或联系家人"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: emptyColumn.implicitHeight + page.theme.gap * 2
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line
                    visible: ReminderVM.today.length === 0

                    Column {
                        id: emptyColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.gap
                        spacing: page.theme.gap

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "heading"
                            text: "今天还没有提醒"
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.inkSoft
                            text: "可以点右上角“新建”，加一条吃药或喝水提醒。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: list.width
                    height: upcomingColumn.implicitHeight + page.theme.gap * 2
                    radius: page.theme.radius
                    color: page.theme.surfaceAlt
                    border.width: 2
                    border.color: page.theme.line
                    visible: ReminderVM.upcoming.length > 0

                    Column {
                        id: upcomingColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.gap
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.inkSoft
                            text: "接下来"
                        }

                        Repeater {
                            model: ReminderVM.upcoming

                            delegate: RowLayout {
                                required property var modelData
                                width: parent.width
                                spacing: page.theme.gapSmall

                                AppText {
                                    Layout.preferredWidth: page.theme.px(96)
                                    theme: page.theme
                                    role: "label"
                                    color: page.theme.primary
                                    text: modelData.dayLabel
                                }

                                AppText {
                                    theme: page.theme
                                    role: "heading"
                                    text: Fmt.hhmm(modelData.dueTs)
                                }

                                AppText {
                                    Layout.fillWidth: true
                                    theme: page.theme
                                    role: "body"
                                    text: modelData.title
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                }
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "到这一天会自动出现在“今天的提醒”里。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    PlanManageSheet {
        id: manageSheet
        objectName: "manageSheet"
        theme: page.theme
    }
}
