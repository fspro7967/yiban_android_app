pragma ComponentBehavior: Bound

import QtQuick

// 轻提示：只用于“已记录 / 已通知”等反馈，不遮挡操作。
Rectangle {
    id: toast

    property Theme theme
    property string message: ""
    property bool shown: false

    z: 300

    width: Math.min(toastText.implicitWidth + theme.px(40), parent.width - theme.px(40))
    height: toastText.implicitHeight + theme.px(24)
    radius: height / 2
    color: theme.dark
    opacity: shown ? 1.0 : 0.0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: 160 }
    }

    AppText {
        id: toastText
        anchors.centerIn: parent
        width: parent.width - toast.theme.px(32)
        theme: toast.theme
        role: "body"
        color: toast.theme.darkInk
        text: toast.message
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }

    property Timer hideTimer: Timer {
        interval: 2400
        onTriggered: toast.shown = false
    }

    function show(text) {
        message = text
        shown = true
        hideTimer.restart()
    }
}
