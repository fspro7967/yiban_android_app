pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 新建提醒：名称、时间、剂量、备注、重复方式；保存前做“名称”和“时间已过”校验。
// 保存按钮放在表单最底部（随内容滚动），不会被键盘顶到屏幕中间误触；
// 校验失败时顶部弹出醒目提示，同时把出错的那一项标红并滚到眼前。
Item {
    id: page

    property Theme theme
    // 默认放在下一个整点：晚上打开也不会一上来就是“时间已过”。
    property string timeText: Fmt.hhmmFromMinutes(Math.min(23 * 60 + 45, (Math.floor(Fmt.nowMinutes / 60) + 1) * 60))
    property string repeatKind: "每天"
    // 过时（错过）后是否还允许点"完成"补做。随餐药等对时间点要求严格的关掉。
    property bool allowLate: true
    property string error: ""
    property string errorField: "" // "name" / "time"

    readonly property int timeMinutes: Fmt.minutesOf(page.timeText)
    readonly property var quickTimes: ["07:00", "08:00", "12:00", "18:00", "20:00", "21:00"]

    // 常用提醒模板：点一下把表单填好，老人或看护人不用打字。
    // times 是建议时刻，取今天还没过的第一个；都过了就不动时间，由用户自己调。
    readonly property var templates: [
        { label: "吃药", icon: "pill", title: "吃药", dose: "1 片", note: "饭后温水送服",
          repeat: "每天", allowLate: false, times: ["08:00", "12:00", "18:00", "21:00"] },
        { label: "测血压", icon: "activity", title: "测血压", dose: "", note: "坐下休息 5 分钟再测",
          repeat: "每天", allowLate: true, times: ["07:00", "19:00"] },
        { label: "复诊", icon: "clipboard", title: "去医院复诊", dose: "", note: "带上病历本和药盒",
          repeat: "每月", allowLate: true, times: ["08:00", "14:00"] },
        { label: "喝水", icon: "droplet", title: "喝水", dose: "1 杯", note: "",
          repeat: "每天", allowLate: true, times: ["10:00", "15:00", "17:00"] }
    ]
    property string templateLabel: ""

    function applyTemplate(t) {
        nameField.text = t.title
        doseField.text = t.dose
        noteField.text = t.note
        page.repeatKind = t.repeat
        page.allowLate = t.allowLate
        for (var i = 0; i < t.times.length; ++i) {
            if (Fmt.minutesOf(t.times[i]) > Fmt.nowMinutes) {
                page.setTime(t.times[i])
                break
            }
        }
        page.templateLabel = t.label
        page.clearError()
    }

    signal back()
    signal saved()

    // 保存前的一句话总结：“每天 08:00 提醒：吃降压药”，设错了一眼能看出来。
    readonly property string repeatPhrase: {
        var d = Fmt.now
        if (page.repeatKind === "每周")
            return "每周" + "日一二三四五六".charAt(d.getDay())
        if (page.repeatKind === "每月")
            return "每月 " + d.getDate() + " 日"
        if (page.repeatKind === "仅一次")
            return "今天"
        return "每天"
    }
    readonly property string summaryTitle: nameField.text.trim().length > 0 ? nameField.text.trim() : "（还没填名称）"
    readonly property bool timePassed: page.timeMinutes <= Fmt.nowMinutes

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


    // 每一步的小标题：深绿圆里一个数字 + 标题。
    component StepTitle: Row {
        id: stepTitle
        property int number: 1
        property string label: ""
        spacing: page.theme.px(10)

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: page.theme.px(32)
            height: width
            radius: width / 2
            color: page.theme.primary

            AppText {
                anchors.centerIn: parent
                theme: page.theme
                role: "label"
                font.pixelSize: page.theme.px(19)
                font.weight: Font.Bold
                color: "#FFFFFF"
                text: stepTitle.number
            }
        }

        AppText {
            anchors.verticalCenter: parent.verticalCenter
            theme: page.theme
            role: "heading"
            font.pixelSize: page.theme.px(26)
            font.weight: Font.Bold
            text: stepTitle.label
        }
    }

    // 时间两侧的圆形 −/+ 键。
    component RoundStep: Rectangle {
        id: roundStep
        property string iconName: ""
        property string caption: ""
        signal clicked()
        property real _last: 0

        // 窄屏 + 特大字号时圆键跟着收小，给中间的时间留出位置。
        Layout.preferredWidth: Math.min(page.theme.px(78), Math.round(form.width * 0.2))
        Layout.preferredHeight: Layout.preferredWidth
        radius: width / 2
        color: page.theme.surface
        border.width: 2
        border.color: page.theme.line
        scale: stepMouse.pressed ? 0.96 : 1

        Column {
            anchors.centerIn: parent
            spacing: 0

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: roundStep.iconName
                size: Math.round(Math.min(page.theme.px(26), roundStep.width * 0.34))
                strokeWidth: 3
                color: page.theme.primary
            }

            AppText {
                anchors.horizontalCenter: parent.horizontalCenter
                theme: page.theme
                role: "label"
                font.pixelSize: Math.round(Math.min(page.theme.px(16), roundStep.width * 0.2))
                font.weight: Font.Bold
                color: page.theme.primary
                text: roundStep.caption
                wrapMode: Text.NoWrap
            }
        }

        MouseArea {
            id: stepMouse
            anchors.fill: parent
            onClicked: {
                var t = Date.now()
                if (Fmt.tooSoon(roundStep._last, t, 250))
                    return
                roundStep._last = t
                roundStep.clicked()
            }
        }

        Accessible.role: Accessible.Button
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
            subtitle: "三步填好"
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

                Icon {
                    id: bannerIcon
                    anchors.verticalCenter: parent.verticalCenter
                    name: "alert"
                    size: page.theme.px(30)
                    color: "#FFFFFF"
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

                Card {
                    objectName: "templateCard"
                    width: form.width
                    height: stepOne.implicitHeight + page.theme.px(18) * 2
                    theme: page.theme

                    Column {
                        id: stepOne
                        width: parent.width - page.theme.px(18) * 2
                        x: page.theme.px(18)
                        y: page.theme.px(18)
                        spacing: page.theme.gapSmall

                        StepTitle { number: 1; label: "做什么" }

                        // 常用模板：点一下把下面都填好，再按需要改。
                        GridLayout {
                            width: parent.width
                            columns: page.theme.scaleLevel >= 2 ? 2 : 4
                            columnSpacing: page.theme.px(8)
                            rowSpacing: page.theme.px(8)

                            Repeater {
                                model: page.templates

                                delegate: Rectangle {
                                    id: tpl
                                    required property var modelData
                                    readonly property bool on: page.templateLabel === modelData.label

                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: page.theme.px(92)
                                    radius: page.theme.radiusSmall
                                    color: on ? page.theme.primarySoft : page.theme.surface
                                    border.width: on ? 3 : 2
                                    border.color: on ? page.theme.primary : page.theme.line

                                    Column {
                                        anchors.centerIn: parent
                                        width: parent.width - page.theme.px(8)
                                        spacing: page.theme.px(4)

                                        Icon {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            name: tpl.modelData.icon
                                            size: page.theme.px(30)
                                            color: tpl.on ? page.theme.primaryInk : page.theme.amber
                                        }

                                        AppText {
                                            width: parent.width
                                            theme: page.theme
                                            role: "label"
                                            font.pixelSize: page.theme.px(20)
                                            font.weight: Font.Bold
                                            color: tpl.on ? page.theme.primaryInk : page.theme.ink
                                            text: tpl.modelData.label
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.NoWrap
                                            fontSizeMode: Text.HorizontalFit
                                            minimumPixelSize: 14
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: page.applyTemplate(tpl.modelData)
                                    }

                                    Accessible.role: Accessible.Button
                                    Accessible.name: "常用提醒：" + modelData.label
                                    Accessible.checked: on
                                }
                            }
                        }

                        Field {
                            id: nameField
                            objectName: "nameField"
                            width: parent.width
                            theme: page.theme
                            label: "名称"
                            placeholder: "例如：吃降压药"
                            invalid: page.errorField === "name"
                            errorText: "请先填写提醒名称，比如“吃降压药”。"
                            onTextChanged: if (text.trim().length > 0) page.clearError("name")
                        }

                        Field {
                            id: doseField
                            width: parent.width
                            theme: page.theme
                            label: "剂量或数量（可以不填）"
                            placeholder: "例如：1 片，半杯"
                        }

                        Field {
                            id: noteField
                            width: parent.width
                            theme: page.theme
                            label: "说明（可以不填）"
                            placeholder: "例如：饭后温水送服"
                        }
                    }
                }

                Card {
                    id: timeCard
                    width: form.width
                    height: timeColumn.implicitHeight + page.theme.px(18) * 2
                    theme: page.theme
                    color: page.errorField === "time" ? page.theme.dangerSoft : page.theme.surface
                    border.width: page.errorField === "time" ? 4 : 1
                    border.color: page.errorField === "time" ? page.theme.danger : page.theme.cardLine

                    Column {
                        id: timeColumn
                        width: parent.width - page.theme.px(18) * 2
                        x: page.theme.px(18)
                        y: page.theme.px(18)
                        spacing: page.theme.gapSmall

                        StepTitle { number: 2; label: "几点提醒" }

                        // 时间两边各一个圆键：左边提前 15 分钟，右边推后 15 分钟。
                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            RoundStep {
                                iconName: "minus"
                                caption: "15 分"
                                Accessible.name: "提前 15 分钟"
                                onClicked: page.bump(-15)
                            }

                            AppText {
                                objectName: "timeText"
                                Layout.fillWidth: true
                                theme: page.theme
                                role: "display"
                                font.pixelSize: page.theme.px(64)
                                font.weight: Font.Bold
                                color: page.errorField === "time" ? page.theme.danger : page.theme.primaryInk
                                text: page.timeText
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.NoWrap
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: page.theme.px(36)
                            }

                            RoundStep {
                                iconName: "plus"
                                caption: "15 分"
                                Accessible.name: "推后 15 分钟"
                                onClicked: page.bump(15)
                            }
                        }

                        AppText {
                            width: parent.width
                            visible: page.errorField === "time"
                            theme: page.theme
                            role: "body"
                            font.weight: Font.DemiBold
                            color: page.theme.dangerInk
                            text: "这个时间已经过了，请点右边的 + 往后调，或点下面的常用时间。"
                            wrapMode: Text.WordWrap
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
                            columns: page.theme.scaleLevel >= 2 ? 2 : 3
                            columnSpacing: page.theme.px(8)
                            rowSpacing: page.theme.px(8)

                            Repeater {
                                model: page.quickTimes

                                // 选中的时间直接填深绿；selected 会加对勾图标，三列放不下，这里自己画选中态。
                                delegate: AppButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: page.theme.touch
                                    theme: page.theme
                                    text: modelData
                                    kind: page.timeText === modelData ? "primary" : "secondary"
                                    onClicked: page.setTime(modelData)
                                }
                            }
                        }
                    }
                }

                Card {
                    width: form.width
                    height: repeatColumn.implicitHeight + page.theme.px(18) * 2
                    theme: page.theme

                    Column {
                        id: repeatColumn
                        width: parent.width - page.theme.px(18) * 2
                        x: page.theme.px(18)
                        y: page.theme.px(18)
                        spacing: page.theme.gapSmall

                        StepTitle { number: 3; label: "多久一次" }

                        Segmented {
                            objectName: "repeatSegmented"
                            width: parent.width
                            theme: page.theme
                            options: ["每天", "每周", "每月", "仅一次"]
                            current: page.repeatKind
                            onPicked: function (value) { page.repeatKind = value }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: page.theme.cardLine
                        }

                        ToggleRow {
                            objectName: "allowLateToggle"
                            width: parent.width
                            theme: page.theme
                            title: "过时还能补做"
                            subtitle: page.allowLate ? "错过后仍可补做" : "错过后不能补做"
                            checked: page.allowLate
                            onToggled: function (value) { page.allowLate = value }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            font.pixelSize: page.theme.px(19)
                            color: page.theme.inkSoft
                            text: "吃药这类要准时的提醒建议关掉，避免过了时间再补吃。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // 一句话总结：保存前再看一眼，设错了容易发现。
                Rectangle {
                    objectName: "saveSummary"
                    width: form.width
                    height: summaryRow.implicitHeight + page.theme.px(16) * 2
                    radius: page.theme.radius
                    color: page.timePassed ? page.theme.amberSoft : page.theme.primarySoft

                    RowLayout {
                        id: summaryRow
                        x: page.theme.px(16)
                        y: page.theme.px(16)
                        width: parent.width - page.theme.px(16) * 2
                        spacing: page.theme.px(12)

                        Icon {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: page.theme.px(4)
                            name: page.timePassed ? "alert" : "bell"
                            size: page.theme.px(28)
                            color: page.timePassed ? page.theme.amberInk : page.theme.primaryInk
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: page.theme.px(2)

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "body"
                                font.weight: Font.Bold
                                color: page.timePassed ? page.theme.amberInk : page.theme.primaryInk
                                text: page.repeatPhrase + " " + page.timeText + " 提醒：" + page.summaryTitle
                                wrapMode: Text.WordWrap
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "label"
                                font.pixelSize: page.theme.px(19)
                                color: page.timePassed ? page.theme.amberInk : page.theme.primaryInk
                                text: page.timePassed
                                    ? "这个时间今天已经过了，请往后调"
                                    : (page.allowLate ? "过了时间还能补做" : "过了时间不能补做")
                                wrapMode: Text.WordWrap
                            }
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
                        iconName: "check"
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
