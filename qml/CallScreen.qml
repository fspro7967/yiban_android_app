pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 紧急呼救确认页：5 秒倒计时，期间可取消；倒计时结束显示“已通知家人”。
Item {
    id: screen

    property Theme theme
    property bool active: false
    property int secondsLeft: 5
    property bool sent: false

    anchors.fill: parent
    visible: active
    z: 60

    signal closed()

    function start() {
        secondsLeft = 5
        sent = false
        active = true
        countdown.restart()
        DeviceVM.speak("正在通知家人", "sos")
    }

    function cancel() {
        if (!active)
            return
        countdown.stop()
        active = false
        Notify.info("已取消呼救")
        closed()
    }

    function finish() {
        countdown.stop()
        active = false
        closed()
    }

    property Timer countdown: Timer {
        interval: 1000
        repeat: true
        onTriggered: {
            screen.secondsLeft -= 1
            if (screen.secondsLeft <= 0) {
                screen.secondsLeft = 0
                screen.sent = true
                screen.countdown.stop()
                DeviceVM.sos(1)
                Notify.info("已通知 " + ProfileVM.caregiverName)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#4A0B09" }
            GradientStop { position: 1.0; color: "#210403" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: screen.theme.pageMargin
        spacing: screen.theme.gap

        AppText {
            Layout.fillWidth: true
            theme: screen.theme
            role: "heading"
            color: screen.theme.reminderAccent
            text: "紧急呼救"
            horizontalAlignment: Text.AlignHCenter
        }

        Item { Layout.fillHeight: true }

        AppText {
            Layout.fillWidth: true
            theme: screen.theme
            role: "hero"
            color: "#FFFFFF"
            horizontalAlignment: Text.AlignHCenter
            text: screen.sent ? "呼救已发出" : "正在通知家人"
        }

        AppText {
            Layout.fillWidth: true
            theme: screen.theme
            role: "display"
            color: screen.theme.reminderAccent
            horizontalAlignment: Text.AlignHCenter
            visible: !screen.sent
            text: "" + screen.secondsLeft
        }

        AppText {
            Layout.fillWidth: true
            theme: screen.theme
            role: "heading"
            color: "#FFFFFF"
            horizontalAlignment: Text.AlignHCenter
            text: screen.sent
                ? "已通知 " + ProfileVM.caregiverName
                : screen.secondsLeft + " 秒后通知家人"
        }

        AppText {
            Layout.fillWidth: true
            theme: screen.theme
            role: "body"
            color: "#FFE3C4"
            horizontalAlignment: Text.AlignHCenter
            text: screen.sent
                ? "电话 " + ProfileVM.caregiverPhone + "，请保持手机畅通"
                : "如果按错了，现在取消还来得及"
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }

        AppButton {
            Layout.fillWidth: true
            Layout.preferredHeight: screen.theme.reminderConfirm
            theme: screen.theme
            text: screen.sent ? "我知道了" : "取消呼救"
            kind: "light"
            onClicked: screen.sent ? screen.finish() : screen.cancel()
        }
    }
}
