pragma Singleton

import QtQuick

// 撤销条队列。撤销窗口是 UI 行为，不改任何业务状态；
// 只是把"马上再发一条反向命令"这个动作延后显示。
// 可以同时存在多条：新的一条叠在上面，各自倒计时、各自撤销，互不覆盖。
QtObject {
    id: root

    // 每项：{ uid, text, secondsLeft, totalSeconds }；下标 0 是最新的一条。
    property ListModel entries: ListModel {}
    readonly property int count: entries.count
    readonly property bool visible: entries.count > 0
    property int maxEntries: 3

    property int _seq: 0
    property var _actions: ({})

    function show(message, undoAction, seconds) {
        var total = seconds === undefined ? 5 : seconds
        root._seq += 1
        var uid = root._seq
        root._actions[uid] = undoAction
        entries.insert(0, { uid: uid, text: message, secondsLeft: total, totalSeconds: total })
        // 叠太多会挡住页面：超出上限时丢掉最早的一条（它的撤销机会也随之结束）。
        while (entries.count > root.maxEntries)
            removeAt(entries.count - 1)
        timer.start()
        return uid
    }

    function indexOf(uid) {
        for (var i = 0; i < entries.count; ++i) {
            if (entries.get(i).uid === uid)
                return i
        }
        return -1
    }

    function removeAt(index) {
        var uid = entries.get(index).uid
        delete root._actions[uid]
        entries.remove(index)
        if (entries.count === 0)
            timer.stop()
    }

    function undo(uid) {
        var index = indexOf(uid)
        if (index < 0)
            return
        var act = root._actions[uid]
        removeAt(index)
        if (act)
            act()
    }

    function dismiss(uid) {
        var index = indexOf(uid)
        if (index >= 0)
            removeAt(index)
    }

    function clear() {
        timer.stop()
        root._actions = ({})
        entries.clear()
    }

    property Timer timer: Timer {
        interval: 1000
        repeat: true
        running: false
        onTriggered: {
            for (var i = root.entries.count - 1; i >= 0; --i) {
                var left = root.entries.get(i).secondsLeft - 1
                if (left <= 0)
                    root.removeAt(i)
                else
                    root.entries.setProperty(i, "secondsLeft", left)
            }
        }
    }
}
