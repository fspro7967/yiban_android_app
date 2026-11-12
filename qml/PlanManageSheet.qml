pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 管理一条提醒（计划）：
//   * 周期提醒：调整周期和时间 / 只删今天这一次 / 删除全部（以后都不提醒）；
//   * 一次性提醒：调整时间 / 删除。
// 所有改动都走 PlanVM 命令，并给 5 秒撤销条。默认焦点不落在危险按钮上。
Item {
    id: sheet

    property Theme theme

    // 打开时从今天的 occurrence 带进来的那一条；plan 是对应的计划快照。
    property var occ: null
    property var plan: null
    property string mode: "menu" // "menu" / "edit" / "deleteAll"

    // 编辑中的草稿
    property string draftKind: "每天"
    property string draftTime: "08:00"
    property var draftWeekdays: []
    property int draftMonthDay: 1
    property bool draftAllowLate: true
    property string editError: ""

    readonly property string kind: plan && plan.rule ? plan.rule.kind : ""
    readonly property bool periodic: kind !== "" && kind !== "once"
    readonly property string title: occ ? occ.title : ""
    readonly property var kinds: ["每天", "每周", "每月", "仅一次"]
    readonly property var weekdayNames: ["一", "二", "三", "四", "五", "六", "日"]
    readonly property int draftMinutes: Fmt.minutesOf(sheet.draftTime)

    anchors.fill: parent
    visible: false
    z: 200

    function findPlan(planId) {
        var list = PlanVM.plans
        for (var i = 0; i < list.length; ++i) {
            if (list[i].planId === planId)
                return list[i]
        }
        return null
    }

    function openFor(item) {
        sheet.occ = item
        sheet.plan = sheet.findPlan(item.planId)
        sheet.mode = "menu"
        sheet.editError = ""
        sheet.visible = true
    }

    function close() {
        sheet.visible = false
        sheet.occ = null
        sheet.plan = null
    }

    function kindLabel(k) {
        if (k === "weekly") return "每周"
        if (k === "monthly") return "每月"
        if (k === "once") return "仅一次"
        return "每天"
    }

    function startEdit() {
        var rule = sheet.plan ? sheet.plan.rule : {}
        var today = new Date()
        sheet.draftKind = sheet.kindLabel(rule.kind)
        sheet.draftTime = sheet.occ ? Fmt.hhmm(sheet.occ.dueTs) : "08:00"
        sheet.draftWeekdays = rule.kind === "weekly" && rule.weekdays
            ? rule.weekdays.slice() : [today.getDay() === 0 ? 7 : today.getDay()]
        sheet.draftMonthDay = rule.kind === "monthly" && rule.day ? rule.day : today.getDate()
        sheet.draftAllowLate = sheet.plan ? sheet.plan.allowLate !== false : true
        sheet.editError = ""
        sheet.mode = "edit"
    }

    function toggleWeekday(d) {
        var list = sheet.draftWeekdays.slice()
        var at = list.indexOf(d)
        if (at >= 0)
            list.splice(at, 1)
        else
            list.push(d)
        list.sort(function (a, b) { return a - b })
        sheet.draftWeekdays = list
        sheet.editError = ""
    }

    function bump(delta) {
        sheet.draftTime = Fmt.hhmmFromMinutes((sheet.draftMinutes + delta + 1440) % 1440)
        sheet.editError = ""
    }

    function buildRule() {
        var today = new Date()
        if (sheet.draftKind === "每周")
            return { kind: "weekly", weekdays: sheet.draftWeekdays, times: [sheet.draftTime] }
        if (sheet.draftKind === "每月")
            return { kind: "monthly", day: sheet.draftMonthDay, times: [sheet.draftTime] }
        if (sheet.draftKind === "仅一次")
            return { kind: "once", at: Qt.formatDateTime(today, "yyyy-MM-dd") + "T" + sheet.draftTime + ":00" }
        return { kind: "daily", times: [sheet.draftTime] }
    }

    function saveEdit() {
        if (!sheet.plan)
            return
        if (sheet.draftKind === "每周" && sheet.draftWeekdays.length === 0) {
            sheet.editError = "每周重复请至少选一天"
            return
        }
        if (sheet.draftKind === "仅一次" && sheet.draftMinutes <= Fmt.nowMinutes) {
            sheet.editError = "仅一次的提醒时间 " + sheet.draftTime + " 已经过了，请往后调"
            return
        }
        var planId = sheet.plan.planId
        var name = sheet.title
        var oldPatch = { rule: sheet.plan.rule, allowLate: sheet.plan.allowLate !== false }
        if (PlanVM.updatePlan(planId, { rule: sheet.buildRule(), allowLate: sheet.draftAllowLate })) {
            sheet.close()
            Undo.show("已调整「" + name + "」", function () {
                PlanVM.updatePlan(planId, oldPatch)
            })
        }
    }

    function skipToday() {
        if (!sheet.occ)
            return
        var planId = sheet.occ.planId
        var name = sheet.title
        sheet.close()
        if (PlanVM.skipToday(planId)) {
            Undo.show("今天不再提醒「" + name + "」", function () {
                PlanVM.unskipToday(planId)
            })
        }
    }

    function deleteAll() {
        if (!sheet.occ)
            return
        var planId = sheet.occ.planId
        var name = sheet.title
        sheet.close()
        if (PlanVM.deletePlan(planId)) {
            Undo.show("已删除「" + name + "」", function () {
                PlanVM.restorePlan(planId)
            })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#99000000"
    }

    MouseArea {
        anchors.fill: parent
        // 吞掉遮罩上的点击，避免误触落到后面的页面
        onClicked: {}
    }

    Rectangle {
        id: card
        width: Math.min(parent.width - sheet.theme.px(32), sheet.theme.px(480))
        height: Math.min(body.implicitHeight + sheet.theme.px(40), parent.height - sheet.theme.px(48))
        anchors.centerIn: parent
        radius: sheet.theme.radius
        color: sheet.theme.surface
        border.width: 2
        border.color: sheet.theme.line

        Flickable {
            id: scroll
            anchors.fill: parent
            anchors.margins: sheet.theme.px(20)
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: body
                width: scroll.width
                spacing: sheet.theme.gap

                AppText {
                    width: parent.width
                    theme: sheet.theme
                    role: "heading"
                    text: sheet.mode === "edit"
                        ? (sheet.periodic ? "调整「" + sheet.title + "」" : "调整「" + sheet.title + "」的时间")
                        : sheet.mode === "deleteAll" ? "删除「" + sheet.title + "」？"
                        : "管理「" + sheet.title + "」"
                    wrapMode: Text.WordWrap
                }

                // ---------- 菜单 ----------
                AppText {
                    width: parent.width
                    visible: sheet.mode === "menu"
                    theme: sheet.theme
                    role: "body"
                    color: sheet.theme.inkSoft
                    text: sheet.plan
                        ? sheet.plan.repeatText + " · " + (sheet.occ ? Fmt.hhmm(sheet.occ.dueTs) : "")
                          + (sheet.plan.allowLate === false ? " · 过时不可补做" : "")
                        : ""
                    wrapMode: Text.WordWrap
                }

                AppButton {
                    objectName: "manageEdit"
                    width: parent.width
                    height: sheet.theme.button
                    visible: sheet.mode === "menu"
                    theme: sheet.theme
                    text: sheet.periodic ? "调整周期和时间" : "调整时间"
                    kind: "secondary"
                    onClicked: sheet.startEdit()
                }

                AppButton {
                    objectName: "manageSkipToday"
                    width: parent.width
                    height: sheet.theme.button
                    visible: sheet.mode === "menu" && sheet.periodic
                    theme: sheet.theme
                    text: "只删除今天这一次"
                    subtitle: "以后照常提醒"
                    kind: "secondary"
                    onClicked: sheet.skipToday()
                }

                AppButton {
                    objectName: "manageDeleteAll"
                    width: parent.width
                    height: sheet.theme.button
                    visible: sheet.mode === "menu"
                    theme: sheet.theme
                    text: sheet.periodic ? "删除全部" : "删除这条提醒"
                    subtitle: sheet.periodic ? "以后都不再提醒" : ""
                    kind: "danger"
                    onClicked: {
                        if (sheet.periodic)
                            sheet.mode = "deleteAll"
                        else
                            sheet.deleteAll()
                    }
                }

                // ---------- 删除全部确认 ----------
                AppText {
                    width: parent.width
                    visible: sheet.mode === "deleteAll"
                    theme: sheet.theme
                    role: "body"
                    color: sheet.theme.inkSoft
                    text: "这条提醒是“" + (sheet.plan ? sheet.plan.repeatText : "") + "”，删除后今天和以后都不再提醒。删错了可以在 5 秒内点「撤销」。"
                    wrapMode: Text.WordWrap
                }

                AppButton {
                    width: parent.width
                    height: sheet.theme.button
                    visible: sheet.mode === "deleteAll"
                    theme: sheet.theme
                    text: "确认删除全部"
                    kind: "danger"
                    onClicked: sheet.deleteAll()
                }

                // ---------- 编辑 ----------
                Column {
                    width: parent.width
                    visible: sheet.mode === "edit"
                    spacing: sheet.theme.gapSmall

                    AppText {
                        width: parent.width
                        theme: sheet.theme
                        role: "label"
                        color: sheet.theme.inkSoft
                        text: "重复方式"
                    }

                    GridLayout {
                        width: parent.width
                        columns: 2
                        columnSpacing: sheet.theme.gapSmall
                        rowSpacing: sheet.theme.gapSmall

                        Repeater {
                            model: sheet.kinds

                            delegate: AppButton {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: sheet.theme.touch
                                theme: sheet.theme
                                text: modelData
                                kind: "secondary"
                                selected: sheet.draftKind === modelData
                                onClicked: {
                                    sheet.draftKind = modelData
                                    sheet.editError = ""
                                }
                            }
                        }
                    }

                    AppText {
                        width: parent.width
                        visible: sheet.draftKind === "每周"
                        theme: sheet.theme
                        role: "label"
                        color: sheet.theme.inkSoft
                        text: "每周哪几天（可多选）"
                    }

                    GridLayout {
                        width: parent.width
                        visible: sheet.draftKind === "每周"
                        columns: 4
                        columnSpacing: sheet.theme.gapSmall
                        rowSpacing: sheet.theme.gapSmall

                        Repeater {
                            model: 7

                            delegate: AppButton {
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredHeight: sheet.theme.touch
                                theme: sheet.theme
                                text: "周" + sheet.weekdayNames[index]
                                kind: "secondary"
                                selected: sheet.draftWeekdays.indexOf(index + 1) >= 0
                                onClicked: sheet.toggleWeekday(index + 1)
                            }
                        }
                    }

                    RowLayout {
                        width: parent.width
                        visible: sheet.draftKind === "每月"
                        spacing: sheet.theme.gapSmall

                        AppButton {
                            Layout.preferredWidth: sheet.theme.touch * 1.3
                            Layout.preferredHeight: sheet.theme.touch
                            theme: sheet.theme
                            text: "−"
                            kind: "secondary"
                            onClicked: sheet.draftMonthDay = sheet.draftMonthDay <= 1 ? 31 : sheet.draftMonthDay - 1
                        }

                        AppText {
                            Layout.fillWidth: true
                            theme: sheet.theme
                            role: "heading"
                            horizontalAlignment: Text.AlignHCenter
                            text: "每月 " + sheet.draftMonthDay + " 日"
                        }

                        AppButton {
                            Layout.preferredWidth: sheet.theme.touch * 1.3
                            Layout.preferredHeight: sheet.theme.touch
                            theme: sheet.theme
                            text: "+"
                            kind: "secondary"
                            onClicked: sheet.draftMonthDay = sheet.draftMonthDay >= 31 ? 1 : sheet.draftMonthDay + 1
                        }
                    }

                    AppText {
                        width: parent.width
                        visible: sheet.draftKind === "每月" && sheet.draftMonthDay > 28
                        theme: sheet.theme
                        role: "label"
                        color: sheet.theme.amberInk
                        text: "没有 " + sheet.draftMonthDay + " 日的月份不会提醒。"
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        width: parent.width
                        spacing: sheet.theme.gapSmall

                        AppText {
                            theme: sheet.theme
                            role: "label"
                            color: sheet.theme.inkSoft
                            text: "提醒时间"
                        }

                        Item { Layout.fillWidth: true }

                        AppText {
                            theme: sheet.theme
                            role: "display"
                            font.pixelSize: sheet.theme.px(48)
                            color: sheet.theme.primaryInk
                            text: sheet.draftTime
                            wrapMode: Text.NoWrap
                        }
                    }

                    RowLayout {
                        width: parent.width
                        spacing: sheet.theme.gapSmall

                        AppButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: sheet.theme.touch
                            theme: sheet.theme
                            text: "−15"
                            subtitle: "分钟"
                            kind: "secondary"
                            onClicked: sheet.bump(-15)
                        }

                        AppButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: sheet.theme.touch
                            theme: sheet.theme
                            text: "+15"
                            subtitle: "分钟"
                            kind: "secondary"
                            onClicked: sheet.bump(15)
                        }
                    }

                    AppText {
                        width: parent.width
                        visible: sheet.draftKind !== "仅一次" && sheet.draftMinutes <= Fmt.nowMinutes
                            && !(sheet.occ && sheet.occ.state === "confirmed")
                            && (sheet.occ ? Fmt.hhmm(sheet.occ.dueTs) !== sheet.draftTime : true)
                        theme: sheet.theme
                        role: "label"
                        color: sheet.theme.amberInk
                        text: "今天的 " + sheet.draftTime + " 已经过了：保存后今天这一次会马上提醒，以后按新时间。"
                        wrapMode: Text.WordWrap
                    }

                    ToggleRow {
                        width: parent.width
                        theme: sheet.theme
                        title: "过时后允许补做"
                        subtitle: sheet.draftAllowLate ? "错过后仍可补做" : "错过后不能补做"
                        checked: sheet.draftAllowLate
                        onToggled: function (value) { sheet.draftAllowLate = value }
                    }

                    AppText {
                        width: parent.width
                        visible: sheet.editError.length > 0
                        theme: sheet.theme
                        role: "body"
                        font.weight: Font.DemiBold
                        color: sheet.theme.dangerInk
                        text: "⚠ " + sheet.editError
                        wrapMode: Text.WordWrap
                    }

                    AppButton {
                        width: parent.width
                        height: sheet.theme.button
                        theme: sheet.theme
                        text: "保存修改"
                        kind: "primary"
                        onClicked: sheet.saveEdit()
                    }
                }

                AppButton {
                    width: parent.width
                    height: sheet.theme.button
                    theme: sheet.theme
                    text: sheet.mode === "menu" ? "取消" : "返回"
                    kind: "secondary"
                    onClicked: {
                        if (sheet.mode === "menu")
                            sheet.close()
                        else
                            sheet.mode = "menu"
                    }
                }
            }
        }
    }
}
