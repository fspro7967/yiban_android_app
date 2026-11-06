pragma Singleton

import QtQuick

// 5 秒撤销条。撤销窗口是 UI 行为，不改任何业务状态；
// 只是把"马上再发一条反向命令"这个动作延后显示。
QtObject {
    id: root

    property bool visible: false
    property string text: ""
    property int secondsLeft: 0
    property var action: null

    function show(message, undoAction, seconds) {
        text = message
        action = undoAction
        secondsLeft = seconds === undefined ? 5 : seconds
        visible = true
        timer.restart()
    }

    function undo() {
        var act = action
        hide()
        if (act)
            act()
    }

    function dismiss() {
        hide()
    }

    function hide() {
        timer.stop()
        visible = false
        action = null
        secondsLeft = 0
    }

    property Timer timer: Timer {
        interval: 1000
        repeat: true
        running: false
        onTriggered: {
            root.secondsLeft -= 1
            if (root.secondsLeft <= 0)
                root.hide()
        }
    }
}
