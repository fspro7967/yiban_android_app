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
    border.width: 0

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

            Icon {
                anchors.centerIn: parent
                name: "phone"
                size: Math.round(parent.width * 0.5)
                strokeWidth: 2.4
                color: control.theme.danger
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
                width: parent.width
                wrapMode: Text.NoWrap
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: control.theme.px(15)
                elide: Text.ElideRight
                // 写明打给谁：老人知道按下去会发生什么，才敢在真需要时按。
                text: control._holding ? "松手即取消"
                    : ProfileVM.caregiverName.length > 0 ? "长按 3 秒，打给" + ProfileVM.caregiverName
                    : "长按 3 秒"
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
