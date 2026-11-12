pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 全屏到点提醒：大时间、大药名、巨大确认键、10 分钟后备键；2 分钟未处理则通知家人。
Item {
    id: screen

    property Theme theme
    property bool active: false
    property var occ: null

    readonly property string caregiverLabel: ProfileVM.caregiverName.length > 0 ? ProfileVM.caregiverName : "家人"
    property int escalationLeft: 120
    property bool escalated: false

    readonly property int snoozeLeft: occ ? occ.snoozeRemaining : 3

    anchors.fill: parent
    visible: active
    z: 50

    signal closed()

    function open(item) {
        occ = item
        escalated = false
        escalationLeft = 120
        active = true
        if (item)
            DeviceVM.speak("提醒您：" + item.title + "。" + (item.dose || ""), "reminder")
    }

    function close() {
        active = false
        escalationTimer.stop()
        closed()
    }

    function confirm() {
        if (!occ)
            return
        ReminderVM.confirm(occ.occurrenceId)
        close()
    }

    function snooze() {
        if (!occ)
            return
        ReminderVM.snooze(occ.occurrenceId)
        close()
    }

    // 提醒开着的时候这一条变成"错过且不允许补做"（有效期走完、时间被往后调）：
    // 完成键已经不会生效，留着全屏页只会把人卡住，直接收起并说明原因。
    property Connections lateWatcher: Connections {
        target: ReminderVM
        enabled: screen.active && screen.occ !== null

        function onTodayChanged() {
            var list = ReminderVM.today
            for (var i = 0; i < list.length; ++i) {
                var item = list[i]
                if (item.occurrenceId !== screen.occ.occurrenceId)
                    continue
                if (item.state === "missed" && !item.canConfirm) {
                    var title = item.title
                    screen.close()
                    Notify.info("「" + title + "」已过时，按要求不能补做")
                }
                return
            }
        }
    }

    property Timer escalationTimer: Timer {
        interval: 1000
        repeat: true
        running: screen.active
        onTriggered: {
            if (screen.escalated)
                return
            screen.escalationLeft -= 1
            if (screen.escalationLeft <= 0) {
                screen.escalationLeft = 0
                screen.escalated = true
                ReminderVM.escalate(screen.occ.occurrenceId)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: screen.theme.reminderBgTop }
            GradientStop { position: 1.0; color: screen.theme.reminderBgBottom }
        }
    }

    Rectangle {
        id: glow
        anchors.fill: parent
        color: "transparent"
        border.width: screen.theme.px(8)
        border.color: screen.theme.reminderAccent
        opacity: 0.6
    }

    SequentialAnimation {
        running: screen.active
        loops: Animation.Infinite
        NumberAnimation { target: glow; property: "opacity"; from: 0.75; to: 0.2; duration: 900 }
        NumberAnimation { target: glow; property: "opacity"; from: 0.2; to: 0.75; duration: 900 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: screen.theme.pageMargin
        spacing: screen.theme.gap

        RowLayout {
            Layout.fillWidth: true
            spacing: screen.theme.gapSmall

            Rectangle {
                Layout.preferredWidth: chipText.implicitWidth + screen.theme.px(26)
                Layout.preferredHeight: screen.theme.px(50)
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.14)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.3)

                AppText {
                    id: chipText
                    anchors.centerIn: parent
                    theme: screen.theme
                    role: "label"
                    color: screen.theme.reminderInk
                    text: DeviceVM.nightQuietActive ? "到点提醒 · 夜间静音" : "到点提醒"
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                Layout.preferredWidth: screen.theme.px(168)
                Layout.preferredHeight: screen.theme.touch
                theme: screen.theme
                text: "再听一遍"
                kind: "outlineLight"
                onClicked: {
                    if (screen.occ)
                        DeviceVM.speak("提醒您：" + screen.occ.title + "。" + (screen.occ.dose || ""), "manual-reminder")
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: info.height
            clip: true

            Column {
                id: info
                width: parent.width
                spacing: screen.theme.px(6)

                AppText {
                    theme: screen.theme
                    role: "display"
                    font.pixelSize: screen.theme.px(52)
                    color: screen.theme.reminderAccent
                    text: screen.occ ? Fmt.hhmm(screen.occ.dueTs) : ""
                    wrapMode: Text.NoWrap
                }

                AppText {
                    width: parent.width
                    theme: screen.theme
                    role: "heading"
                    font.pixelSize: screen.theme.px(38)
                    color: screen.theme.reminderInk
                    text: screen.occ ? screen.occ.title : ""
                    wrapMode: Text.WordWrap
                }

                AppText {
                    width: parent.width
                    visible: text !== ""
                    theme: screen.theme
                    role: "heading"
                    color: screen.theme.reminderInk
                    opacity: 0.95
                    text: screen.occ ? (screen.occ.dose || "") : ""
                }

                AppText {
                    width: parent.width
                    visible: text !== ""
                    theme: screen.theme
                    role: "body"
                    color: screen.theme.reminderInk
                    opacity: 0.8
                    text: screen.occ ? (screen.occ.note || "") : ""
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: escalationColumn.implicitHeight + screen.theme.px(24)
            radius: screen.theme.radiusSmall
            color: Qt.rgba(0, 0, 0, 0.26)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.16)

            Column {
                id: escalationColumn
                width: parent.width - screen.theme.px(28)
                anchors.centerIn: parent
                spacing: screen.theme.px(8)

                AppText {
                    width: parent.width
                    theme: screen.theme
                    role: "body"
                    color: screen.theme.reminderInk
                    text: screen.escalated
                        ? "已通知 " + screen.caregiverLabel + "，请尽快确认"
                        : Fmt.countdownText(Math.ceil(screen.escalationLeft / 60)) + "通知 " + screen.caregiverLabel
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    height: screen.theme.px(8)
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.18)

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, 1 - screen.escalationLeft / 120))
                        height: parent.height
                        radius: height / 2
                        color: screen.theme.reminderAccent
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: screen.theme.gapSmall

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: screen.theme.reminderConfirm
                theme: screen.theme
                text: screen.occ && screen.occ.title.indexOf("药") >= 0 ? "✓ 我已吃好" : "✓ 我已经完成"
                kind: "light"
                onClicked: screen.confirm()
            }

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: screen.theme.buttonLarge
                theme: screen.theme
                text: "稍后 10 分钟"
                kind: "outlineLight"
                enabled: !screen.escalated && screen.snoozeLeft > 0
                onClicked: screen.snooze()
            }

            AppText {
                Layout.fillWidth: true
                theme: screen.theme
                role: "label"
                color: screen.theme.reminderInk
                opacity: 0.75
                horizontalAlignment: Text.AlignHCenter
                text: screen.escalated ? "已通知家人，请尽快确认"
                    : screen.snoozeLeft > 0 ? "还可以推迟 " + screen.snoozeLeft + " 次"
                    : "已推迟 3 次，请先吃药或联系家人"
            }
        }
    }
}
