pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 新建提醒：名称、时间、剂量、备注、重复方式；保存前做“名称”和“时间已过”校验。
Item {
    id: page

    property Theme theme
    property MockBackend store
    property string timeText: "12:00"
    property string repeatKind: "每天"
    property string error: ""

    readonly property int timeMinutes: page.store.minutesOf(page.timeText)
    readonly property var quickTimes: ["07:00", "08:00", "12:00", "18:00"]

    signal back()
    signal saved()

    function bump(delta) {
        var m = (page.timeMinutes + delta + 1440) % 1440
        page.timeText = page.store.pad2(Math.floor(m / 60)) + ":" + page.store.pad2(m % 60)
        page.error = ""
    }

    function save() {
        var name = nameField.text.trim()
        if (name.length === 0) {
            page.error = "请先填写提醒名称，比如“吃降压药”。"
            nameField.forceFocus()
            return
        }
        if (page.timeMinutes <= page.store.nowMinutes) {
            page.error = "这个时间已经过了，请用 +15 分钟往后调。"
            return
        }
        page.error = ""
        page.store.addReminder(name, page.timeText, doseField.text.trim(), noteField.text.trim(), page.repeatKind)
        page.saved()
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
            title: "新建提醒"
            subtitle: "到点会提醒"
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
            contentHeight: form.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: form
                width: scroll.width
                topPadding: page.theme.gapSmall
                bottomPadding: page.theme.gap * 2
                spacing: page.theme.gap

                Field {
                    id: nameField
                    width: form.width
                    theme: page.theme
                    label: "提醒名称"
                    placeholder: "例如：吃降压药"
                }

                Rectangle {
                    width: form.width
                    height: timeColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: timeColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "提醒时间"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(58)
                                color: page.theme.primaryInk
                                text: page.timeText
                                wrapMode: Text.NoWrap
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "−15"
                                            subtitle: "分钟"
                                kind: "secondary"
                                onClicked: page.bump(-15)
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "+15"
                                            subtitle: "分钟"
                                kind: "secondary"
                                onClicked: page.bump(15)
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "常用时间"
                        }

                        GridLayout {
                            width: parent.width
                            columns: 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            Repeater {
                                model: page.quickTimes

                                delegate: AppButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.touch
                                    theme: page.theme
                                    text: modelData
                                    kind: "secondary"
                                    selected: page.timeText === modelData
                                    onClicked: {
                                        page.timeText = modelData
                                        page.error = ""
                                    }
                                }
                            }
                        }
                    }
                }

                Field {
                    id: doseField
                    width: form.width
                    theme: page.theme
                    label: "剂量或数量（可不填）"
                    placeholder: "例如：1 片，半杯"
                }

                Field {
                    id: noteField
                    width: form.width
                    theme: page.theme
                    label: "备注（可不填）"
                    placeholder: "例如：饭后吃，温水送服"
                }

                Rectangle {
                    width: form.width
                    height: repeatColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: repeatColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            text: "重复方式"
                        }

                        GridLayout {
                            width: parent.width
                            columns: 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            Repeater {
                                model: ["每天", "每周", "每月", "仅一次"]

                                delegate: AppButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.touch
                                    theme: page.theme
                                    text: modelData
                                    kind: "secondary"
                                    selected: page.repeatKind === modelData
                                    onClicked: page.repeatKind = modelData
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: form.width
                    height: errorText.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radiusSmall
                    color: page.theme.dangerSoft
                    border.width: 2
                    border.color: page.theme.danger
                    visible: page.error !== ""

                    AppText {
                        id: errorText
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(10)
                        theme: page.theme
                        role: "body"
                        color: page.theme.dangerInk
                        text: page.error
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.bottomMargin: page.theme.gap
            spacing: page.theme.px(6)

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: page.theme.buttonLarge
                theme: page.theme
                text: "保存提醒"
                kind: "primary"
                onClicked: page.save()
            }

            AppText {
                Layout.fillWidth: true
                theme: page.theme
                role: "label"
                color: page.theme.inkSoft
                horizontalAlignment: Text.AlignHCenter
                text: "保存后会加入“今天的提醒”"
            }
        }
    }
}
