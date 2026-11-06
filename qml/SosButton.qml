pragma ComponentBehavior: Bound

import QtQuick

// 长按 3 秒的 SOS 条：按住时进度条走满才触发，松手即取消（需求：防误触）。
Rectangle {
    id: control

    property Theme theme
    property int holdMs: 3000

    property int _held: 0
    property bool _holding: false

    signal activated()

    implicitHeight: theme.px(92)
    radius: theme.radius
    color: _holding ? "#9E1B14" : theme.danger
    border.width: 2
    border.color: "#7F0F0A"

    Row {
        anchors.fill: parent
        anchors.margins: control.theme.px(12)
        spacing: control.theme.px(14)

        Rectangle {
            width: control.theme.px(56)
            height: width
            radius: width / 2
            color: "#FFFFFF"
            anchors.verticalCenter: parent.verticalCenter

            AppText {
                anchors.centerIn: parent
                theme: control.theme
                role: "heading"
                color: control.theme.danger
                text: "!"
            }
        }

        Column {
            width: parent.width - control.theme.px(56) - control.theme.px(14) - control.theme.px(4)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            AppText {
                theme: control.theme
                role: "button"
                font.weight: Font.DemiBold
                color: "#FFFFFF"
                text: control._holding ? "继续按住 " + Math.ceil((control.holdMs - control._held) / 1000) + " 秒" : "紧急呼救"
            }

            AppText {
                theme: control.theme
                role: "label"
                color: "#FFFFFF"
                opacity: 0.9
                text: control._holding ? "松手即取消" : "长按 3 秒"
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 2
        width: Math.max(0, (parent.width - 4) * Math.min(1, control._held / control.holdMs))
        height: control.theme.px(6)
        radius: height / 2
        color: control.theme.reminderAccent
        visible: control._holding
    }

    property Timer holdTimer: Timer {
        interval: 50
        repeat: true
        onTriggered: {
            control._held += 50
            if (control._held >= control.holdMs) {
                control._held = 0
                control._holding = false
                control.holdTimer.stop()
                control.activated()
            }
        }
    }

    function startHold() {
        _held = 0
        _holding = true
        holdTimer.restart()
    }

    function stopHold() {
        _holding = false
        _held = 0
        holdTimer.stop()
    }

    MouseArea {
        anchors.fill: parent
        onPressed: control.startHold()
        onReleased: control.stopHold()
        onCanceled: control.stopHold()
    }

    Accessible.role: Accessible.Button
    Accessible.name: "紧急呼救，按住三秒"
}
