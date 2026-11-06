pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 设置：三档字号、语音、夜间安静、家人电话、配对状态、演示工具。
Item {
    id: page

    property Theme theme
    property MockBackend store

    signal back()
    signal sosRequested()

    function callCaregiver() {
        var number = page.store.caregiverPhone.replace(/\s/g, "")
        if (!Qt.openUrlExternally("tel:" + number))
            page.store.toast("演示版没有接入拨号，号码：" + page.store.caregiverPhone)
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
            title: "设置"
            subtitle: "字大、声大"
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
                bottomPadding: page.theme.gap * 2

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "显示"
                }

                Rectangle {
                    width: list.width
                    height: fontColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: fontColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            text: "字号大小"
                        }

                        ColumnLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            Repeater {
                                model: [
                                    { "label": "标准字号", "level": 0 },
                                    { "label": "大字号（推荐）", "level": 1 },
                                    { "label": "特大字号", "level": 2 }
                                ]

                                delegate: AppButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.touch
                                    theme: page.theme
                                    text: modelData.label
                                    kind: "secondary"
                                    selected: page.theme.scaleLevel === modelData.level
                                    onClicked: page.theme.scaleLevel = modelData.level
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: sampleText.implicitHeight + page.theme.gap
                            radius: page.theme.radiusSmall
                            color: page.theme.surfaceAlt

                            AppText {
                                id: sampleText
                                width: parent.width - page.theme.gap
                                anchors.centerIn: parent
                                theme: page.theme
                                role: "body"
                                color: page.theme.ink
                                text: "示例：今天 12:00 吃降糖药"
                                wrapMode: Text.WordWrap
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "选择后，所有页面的文字和按钮会一起变大。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "提醒与声音"
                }

                Rectangle {
                    width: list.width
                    height: voiceColumn.implicitHeight + page.theme.gap
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: voiceColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(6)

                        ToggleRow {
                            width: parent.width
                            theme: page.theme
                            title: "语音播报"
                            subtitle: "到点会读"
                            checked: page.store.voiceEnabled
                            onToggled: function (value) {
                                page.store.voiceEnabled = value
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: page.theme.line
                        }

                        ToggleRow {
                            width: parent.width
                            theme: page.theme
                            title: "夜间安静"
                            subtitle: "22:00–07:00"
                            checked: page.store.nightQuiet
                            onToggled: function (value) {
                                page.store.nightQuiet = value
                            }
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "家人"
                }

                Rectangle {
                    width: list.width
                    height: familyColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: familyColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "heading"
                            text: page.store.caregiverName
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.inkSoft
                            text: "紧急联系人 · " + page.store.caregiverPhone
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "给家人打电话"
                            kind: "secondary"
                            onClicked: callDialog.open()
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "设备"
                }

                Rectangle {
                    width: list.width
                    height: deviceColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: deviceColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "配对状态"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: page.theme.primary
                                font.weight: Font.DemiBold
                                text: "已连接（演示）"
                            }
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "重新配对"
                            kind: "secondary"
                            onClicked: page.store.toast("演示版：配对接口尚未接入")
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "演示工具（答辩时使用）"
                }

                Rectangle {
                    width: list.width
                    height: demoColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surfaceAlt
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: demoColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "预览到点提醒"
                            kind: "secondary"
                            onClicked: page.store.triggerDemoReminder()
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "预览紧急呼救"
                            kind: "secondary"
                            onClicked: page.sosRequested()
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "恢复演示数据"
                            kind: "secondary"
                            onClicked: resetDialog.open()
                        }
                    }
                }

                AppText {
                    width: list.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    horizontalAlignment: Text.AlignHCenter
                    text: "一伴 · 老人提醒演示版 0.1\n数据只保存在本机"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    ConfirmDialog {
        id: callDialog
        theme: page.theme
        title: "给家人打电话"
        message: "要拨打 " + page.store.caregiverName + " 的电话吗？\n" + page.store.caregiverPhone
        confirmText: "拨打"
        cancelText: "取消"
        onAccepted: page.callCaregiver()
    }

    ConfirmDialog {
        id: resetDialog
        theme: page.theme
        title: "恢复演示数据"
        message: "今天的提醒、喝水、睡眠记录都会回到初始状态，方便重新演示。"
        confirmText: "恢复"
        cancelText: "取消"
        danger: true
        onAccepted: page.store.resetDemo()
    }
}
