pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 颐伴 · 老人提醒端（Qt 6 / QML）
// 结构：安全区 → StackView 页面栈 → 全屏提醒/呼救覆盖层 → 撤销条/Toast。
ApplicationWindow {
    id: window

    width: 412
    height: 915
    visible: true
    title: "颐伴 · 提醒"
    color: window.theme.bg

    Theme { id: appTheme }

    readonly property Theme theme: appTheme

    readonly property int safeTop: SafeArea.margins.top
    readonly property int safeBottom: SafeArea.margins.bottom

    property real lastNavigate: 0

    function navigate(pageName) {
        var t = Date.now()
        if (t - window.lastNavigate < 400)
            return
        window.lastNavigate = t

        var target = null
        if (pageName === "reminders")
            target = remindersComponent
        else if (pageName === "add")
            target = addComponent
        else if (pageName === "record")
            target = recordComponent
        else if (pageName === "stats")
            target = statsComponent
        else if (pageName === "settings")
            target = settingsComponent
        else if (pageName === "caregiverGate")
            target = caregiverGateComponent
        else if (pageName === "admin")
            target = adminComponent
        if (!target)
            return
        stack.push(target)
    }

    function goBack() {
        if (stack.depth > 1)
            stack.pop()
    }

    function goHome() {
        if (stack.depth > 1)
            stack.pop(null)
    }

    function showAdmin() {
        if (stack.depth > 1)
            stack.replace(adminComponent)
    }

    function startSos() {
        if (callScreen.active)
            return
        callScreen.start()
    }

    function stopSos() {
        if (callScreen.active)
            callScreen.cancel()
    }

    Item {
        id: root
        anchors.fill: parent
        focus: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: window.safeTop
            }

            StackView {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                initialItem: homeComponent

                background: Rectangle {
                    color: window.theme.bg
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: window.safeBottom
            }
        }

        Keys.onBackPressed: function (event) {
            if (callScreen.active) {
                window.stopSos()
                event.accepted = true
                return
            }
            if (reminderScreen.active) {
                // 提醒页强制二选一，避免误按返回键把提醒藏起来
                event.accepted = true
                return
            }
            if (stack.currentItem && stack.currentItem.objectName === "caregiverGate") {
                // 缓冲页按返回键等同退出，直接回主界面
                window.goHome()
                event.accepted = true
                return
            }
            if (stack.depth > 1) {
                window.goBack()
                event.accepted = true
                return
            }
            event.accepted = false
        }
    }

    Component {
        id: homeComponent
        HomePage {
            theme: window.theme
            onOpenPage: function (pageName) {
                window.navigate(pageName)
            }
            onSosActivated: window.startSos()
        }
    }

    Component {
        id: remindersComponent
        RemindersPage {
            theme: window.theme
            onBack: window.goBack()
            onAddRequested: window.navigate("add")
        }
    }

    Component {
        id: addComponent
        AddReminderPage {
            theme: window.theme
            onBack: window.goBack()
            onSaved: window.goBack()
        }
    }

    Component {
        id: recordComponent
        RecordPage {
            theme: window.theme
            onBack: window.goBack()
        }
    }

    Component {
        id: statsComponent
        StatsPage {
            theme: window.theme
            onBack: window.goBack()
        }
    }

    Component {
        id: settingsComponent
        SettingsPage {
            theme: window.theme
            onBack: window.goBack()
            onSosRequested: window.startSos()
            onAdminRequested: window.navigate("caregiverGate")
        }
    }

    Component {
        id: caregiverGateComponent
        CaregiverGatePage {
            objectName: "caregiverGate"
            theme: window.theme
            onUnlocked: window.showAdmin()
            onCancelled: window.goHome()
        }
    }

    Component {
        id: adminComponent
        AdminPage {
            theme: window.theme
            onBack: window.goBack()
        }
    }

    // 全屏到点提醒（必须做出选择，返回键不关闭）
    ReminderScreen {
        id: reminderScreen
        objectName: "reminderScreen"
        anchors.fill: parent
        theme: window.theme
    }

    // 紧急呼救 5 秒确认
    CallScreen {
        id: callScreen
        objectName: "callScreen"
        anchors.fill: parent
        theme: window.theme
    }

    // 底部撤销条：5 秒内可撤销确认/记录
    UndoBar {
        id: undoBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: window.theme.pageMargin
        anchors.rightMargin: window.theme.pageMargin
        anchors.bottomMargin: window.safeBottom + window.theme.gap
        theme: window.theme
        active: Undo.visible
        message: Undo.text
        secondsLeft: Undo.secondsLeft
        onUndo: Undo.undo()
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: window.safeTop + window.theme.gap
        theme: window.theme
    }

    // VM 只发结构化结果：toast 是给用户看的一句话，commandRejected 带稳定错误码。
    Connections {
        target: ReminderVM

        function onToast(text) { toast.show(text) }
        function onCommandRejected(code, message) { toast.show(message) }

        function onReminderDue(occurrence) {
            if (reminderScreen.active || callScreen.active)
                return
            reminderScreen.open(occurrence)
        }
    }

    Connections {
        target: PlanVM

        function onToast(text) { toast.show(text) }
        function onCommandRejected(code, message) { toast.show(message) }
    }

    Connections {
        target: RecordVM

        function onToast(text) { toast.show(text) }
        function onCommandRejected(code, message) { toast.show(message) }
    }

    Connections {
        target: DeviceVM

        function onToast(text) { toast.show(text) }
        function onCommandRejected(code, message) { toast.show(message) }
    }

    Connections {
        target: SyncVM

        function onToast(text) { toast.show(text) }
        function onCommandRejected(code, message) { toast.show(message) }
    }

    Connections {
        target: Notify

        function onInfo(text) { toast.show(text) }
    }

    Component.onCompleted: root.forceActiveFocus()
}
