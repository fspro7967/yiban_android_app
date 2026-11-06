pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import "../../qml"

// 仅用于桌面离屏预览/截图，不参与应用运行时。
Window {
    id: root

    width: 412
    height: 915
    visible: true
    color: "#FAF4E8"

    property string outPath: ""
    property string pageName: "home"
    property var pageObj: null

    Theme { id: theme }
    MockBackend { id: store }

    Item {
        id: host
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: theme.bg
            z: -1
        }
    }

    function argValue(prefix) {
        var args = Qt.application.arguments
        for (var i = 0; i < args.length; i++) {
            if (args[i].indexOf(prefix) === 0)
                return args[i].substring(prefix.length)
        }
        return ""
    }

    function showPage() {
        if (pageName === "main") {
            var mainComp = Qt.createComponent("../../qml/Main.qml")
            if (mainComp.status !== Component.Ready) {
                console.error("main create failed:", mainComp.errorString())
                Qt.quit()
                return
            }
            pageObj = mainComp.createObject(null, {})
            if (!pageObj) {
                console.error("main object failed")
                Qt.quit()
                return
            }
            var nav = argValue("--nav=")
            if (nav !== "") {
                navTimer.targetPage = nav
                navTimer.start()
            }
            return
        }

        var path = ""
        if (pageName === "reminders")
            path = "../../qml/RemindersPage.qml"
        else if (pageName === "add")
            path = "../../qml/AddReminderPage.qml"
        else if (pageName === "record")
            path = "../../qml/RecordPage.qml"
        else if (pageName === "stats")
            path = "../../qml/StatsPage.qml"
        else if (pageName === "settings")
            path = "../../qml/SettingsPage.qml"
        else if (pageName === "header")
            path = "../../qml/HeaderBar.qml"
        else if (pageName === "reminder")
            path = "../../qml/ReminderScreen.qml"
        else if (pageName === "call")
            path = "../../qml/CallScreen.qml"
        else
            path = "../../qml/HomePage.qml"

        console.log("loading", path)
        var comp = Qt.createComponent(path)
        if (comp.status !== Component.Ready) {
            console.error("create failed:", path, comp.errorString())
            Qt.quit()
            return
        }
        var props = { theme: theme, store: store }
        if (pageName === "reminder") {
            props.active = true
            props.occ = store.reminders[2]
        } else if (pageName === "call") {
            props.active = true
        }
        console.log("creating object")
        pageObj = comp.createObject(host, props)
        console.log("created object")
        if (!pageObj) {
            console.error("object failed:", path)
            Qt.quit()
            return
        }
        pageObj.anchors.fill = host
        if (pageName === "reminder")
            pageObj.open(store.reminders[2])
        else if (pageName === "call")
            pageObj.start()
    }

    Timer {
        interval: 1200
        running: true
        onTriggered: {
            root.outPath = root.argValue("--out=")
            root.pageName = root.argValue("--page=") || "home"
            root.showPage()
            grabTimer.interval = root.pageName === "main" ? 2200 : 1400
            grabTimer.start()
        }
    }

    Timer {
        id: navTimer
        property string targetPage: ""
        interval: 900
        onTriggered: {
            if (root.pageObj && root.pageObj.navigate)
                root.pageObj.navigate(targetPage)
        }
    }

    Timer {
        id: grabTimer
        interval: 1400
        onTriggered: {
            console.log("grab start")
            var target = host
            if (root.pageName === "main" && root.pageObj) {
                var ci = root.pageObj.contentItem
                target = (ci && ci.children.length > 0) ? ci.children[0] : ci
            }
            console.log("calling grabToImage")
            target.grabToImage(function (result) {
                console.log("grab callback")
                var path = root.outPath !== "" ? root.outPath : "/tmp/shot.png"
                if (result.saveToFile(path))
                    console.log("saved:", path)
                else
                    console.error("save failed:", path)
                Qt.quit()
            }, Qt.size(root.width, root.height))
        }
    }

    Timer {
        interval: 4000
        running: true
        onTriggered: Qt.quit()
    }

    Timer {
        interval: 500
        repeat: true
        running: true
        onTriggered: console.log("alive")
    }

    Component.onCompleted: {
        outPath = argValue("--out=")
        pageName = argValue("--page=") || "home"
        var sc = argValue("--scale=")
        if (sc !== "")
            theme.scaleLevel = parseInt(sc)
        var w = argValue("--w=")
        var h = argValue("--h=")
        if (w !== "")
            width = parseInt(w)
        if (h !== "")
            height = parseInt(h)
    }
}
