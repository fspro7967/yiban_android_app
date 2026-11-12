pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 新建提醒：名称、时间、剂量、备注、重复方式；保存前做“名称”和“时间已过”校验。
// 保存按钮放在表单最底部（随内容滚动），不会被键盘顶到屏幕中间误触；
// 校验失败时顶部弹出醒目提示，同时把出错的那一项标红并滚到眼前。
Item {
    id: page

    property Theme theme
    property string timeText: "12:00"
    property string repeatKind: "每天"
    // 过时（错过）后是否还允许点"完成"补做。随餐药等对时间点要求严格的关掉。
    property bool allowLate: true
    property string error: ""
    property string errorField: "" // "name" / "time"

    readonly property int timeMinutes: Fmt.minutesOf(page.timeText)
    readonly property var quickTimes: ["07:00", "08:00", "12:00", "18:00"]

    signal back()
    signal saved()

    function clearError(field) {
        if (field === undefined || page.errorField === field) {
            page.error = ""
            page.errorField = ""
            banner.hide()
        }
    }

    function setTime(text) {
        page.timeText = text
        page.clearError("time")
    }

    function bump(delta) {
        var m = (page.timeMinutes + delta + 1440) % 1440
        page.setTime(Fmt.hhmmFromMinutes(m))
    }

    function scrollTo(item) {
        var target = item.mapToItem(form, 0, 0).y - page.theme.gapSmall
        scroll.contentY = Math.max(0, Math.min(target, scroll.contentHeight - scroll.height))
    }

    function fail(field, message) {
        page.errorField = field
        page.error = message
        banner.show(message)
    }

    function save() {
        var name = nameField.text.trim()
        if (name.length === 0) {
            page.fail("name", "还没有填写提醒名称")
            page.scrollTo(nameField)
            nameField.forceFocus()
            return
        }
        if (page.timeMinutes <= Fmt.nowMinutes) {
            page.fail("time", "提醒时间 " + page.timeText + " 已经过了，请往后调")
            Qt.inputMethod.hide()
            page.scrollTo(timeCard)
            return
        }
        page.clearError()
        // rule 的形状是冻结契约（docs/端上接口规范_v1.1.md 第 2 节）：
        // 端上只支持 daily / weekly / monthly / once，见 docs/端上接口规范_v1.1.md §6。
        var rule = { kind: "daily", times: [page.timeText] }
        var today = new Date()
        if (page.repeatKind === "每周")
            rule = { kind: "weekly", weekdays: [today.getDay() === 0 ? 7 : today.getDay()], times: [page.timeText] }
        else if (page.repeatKind === "每月")
            rule = { kind: "monthly", day: today.getDate(), times: [page.timeText] }
        else if (page.repeatKind === "仅一次")
            rule = { kind: "once", at: Qt.formatDateTime(today, "yyyy-MM-dd") + "T" + page.timeText + ":00" }

        // 失败时 Main 会统一弹 commandRejected 的文案，这里不返回，避免"没建成却回到列表"。
        if (PlanVM.createPlan({
                title: name,
                dose: doseField.text.trim(),
                note: noteField.text.trim(),
                rule: rule,
                windowMin: 30,
                escalateAfterMin: 15,
                allowLate: page.allowLate
            }))
            page.saved()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        HeaderBar {
            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.topMargin: page.theme.gapSmall
            theme: page.theme
            title: "新建提醒"
            subtitle: "到点会提醒"
            showBack: true
            onBack: page.back()
        }

        // 顶部醒目提示：校验失败时出现在标题栏正下方（把表单往下推，不遮挡），几秒后自动收起，点一下也能关掉。
        Rectangle {
            id: banner
            objectName: "formBanner"

            property string message: ""
            property bool shown: false

            function show(text) {
                message = text
                shown = true
                bannerTimer.restart()
            }

            function hide() {
                shown = false
                bannerTimer.stop()
            }

            Layout.fillWidth: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            Layout.topMargin: page.theme.px(6)
            Layout.preferredHeight: bannerRow.implicitHeight + page.theme.gap * 1.2
            radius: page.theme.radiusSmall
            color: page.theme.danger
            border.width: 2
            border.color: page.theme.dangerInk
            visible: shown

            Row {
                id: bannerRow
                x: page.theme.gap
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - page.theme.gap * 2
                spacing: page.theme.gapSmall

                AppText {
                    id: bannerIcon
                    theme: page.theme
                    role: "heading"
                    color: "#FFFFFF"
                    text: "⚠"
                }

                AppText {
                    width: parent.width - bannerIcon.width - page.theme.gapSmall
                    anchors.verticalCenter: parent.verticalCenter
                    theme: page.theme
                    role: "body"
                    font.weight: Font.DemiBold
                    color: "#FFFFFF"
                    text: banner.message
                    wrapMode: Text.WordWrap
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: banner.hide()
            }

            Timer {
                id: bannerTimer
                interval: 4000
                onTriggered: banner.shown = false
            }

            Accessible.role: Accessible.AlertMessage
            Accessible.name: message
        }

        Flickable {
            id: scroll
            objectName: "addScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            contentWidth: width
            contentHeight: form.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: form
                width: scroll.width
                topPadding: page.theme.gapSmall
                bottomPadding: page.theme.gap * 2
                spacing: page.theme.gap

                Field {
                    id: nameField
                    objectName: "nameField"
                    width: form.width
                    theme: page.theme
                    label: "提醒名称"
                    placeholder: "例如：吃降压药"
                    invalid: page.errorField === "name"
                    errorText: "请先填写提醒名称，比如“吃降压药”。"
                    onTextChanged: if (text.trim().length > 0) page.clearError("name")
                }

                Rectangle {
                    id: timeCard
                    width: form.width
                    height: timeColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.errorField === "time" ? page.theme.dangerSoft : page.theme.surface
                    border.width: page.errorField === "time" ? 4 : 2
                    border.color: page.errorField === "time" ? page.theme.danger : page.theme.line

                    Column {
                        id: timeColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "提醒时间"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(58)
                                color: page.errorField === "time" ? page.theme.danger : page.theme.primaryInk
                                text: page.timeText
                                wrapMode: Text.NoWrap
                            }
                        }

                        AppText {
                            width: parent.width
                            visible: page.errorField === "time"
                            theme: page.theme
                            role: "body"
                            font.weight: Font.DemiBold
                            color: page.theme.dangerInk
                            text: "⚠ 这个时间已经过了，请用 +15 分钟往后调，或点下面的常用时间。"
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "−15"
                                            subtitle: "分钟"
                                kind: "secondary"
                                onClicked: page.bump(-15)
                            }

                            AppButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: page.theme.touch
                                theme: page.theme
                                text: "+15"
                                            subtitle: "分钟"
                                kind: "secondary"
                                onClicked: page.bump(15)
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "常用时间"
                        }

                        GridLayout {
                            width: parent.width
                            columns: 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            Repeater {
                                model: page.quickTimes

                                delegate: AppButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.touch
                                    theme: page.theme
                                    text: modelData
                                    kind: "secondary"
                                    selected: page.timeText === modelData
                                    onClicked: page.setTime(modelData)
                                }
                            }
                        }
                    }
                }

                Field {
                    id: doseField
                    width: form.width
                    theme: page.theme
                    label: "剂量或数量（可不填）"
                    placeholder: "例如：1 片，半杯"
                }

                Field {
                    id: noteField
                    width: form.width
                    theme: page.theme
                    label: "备注（可不填）"
                    placeholder: "例如：饭后吃，温水送服"
                }

                Rectangle {
                    width: form.width
                    height: repeatColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: repeatColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            text: "重复方式"
                        }

                        GridLayout {
                            width: parent.width
                            columns: 2
                            columnSpacing: page.theme.gapSmall
                            rowSpacing: page.theme.gapSmall

                            Repeater {
                                model: ["每天", "每周", "每月", "仅一次"]

                                delegate: AppButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.touch
                                    theme: page.theme
                                    text: modelData
                                    kind: "secondary"
                                    selected: page.repeatKind === modelData
                                    onClicked: page.repeatKind = modelData
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: form.width
                    height: lateColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: lateColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.px(4)

                        ToggleRow {
                            objectName: "allowLateToggle"
                            width: parent.width
                            theme: page.theme
                            title: "过时后允许补做"
                            subtitle: page.allowLate ? "错过后仍可补做" : "错过后不能补做"
                            checked: page.allowLate
                            onToggled: function (value) { page.allowLate = value }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "吃药等对时间要求严格的提醒建议关闭，避免过了时间再补吃。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // 保存按钮是表单的最后一项：要滚到最底下才看得到，键盘弹出时不会跟着浮上来。
                Column {
                    width: form.width
                    topPadding: page.theme.gapSmall
                    spacing: page.theme.px(6)

                    AppButton {
                        objectName: "saveButton"
                        width: parent.width
                        height: page.theme.buttonLarge
                        theme: page.theme
                        text: "保存提醒"
                        kind: "primary"
                        onClicked: page.save()
                    }

                    AppText {
                        width: parent.width
                        theme: page.theme
                        role: "label"
                        color: page.theme.inkSoft
                        horizontalAlignment: Text.AlignHCenter
                        text: "保存后会加入“今天的提醒”"
                    }
                }
            }
        }
    }
}
