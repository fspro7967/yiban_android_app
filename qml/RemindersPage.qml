pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Layouts

// 今日提醒清单：按“待完成 → 已错过 → 已完成”分组，要做的永远在最上面。
// 待完成是大卡（完成 / 推迟两个大按钮）；已完成压成一行，只留“撤销”；
// 确认/推迟都只走显式按钮，点卡片本身不执行动作。
Item {
    id: page

    property Theme theme

    signal back()
    signal addRequested()

    // 说明小条关掉后记住，不用每次进来都看一遍。
    Settings {
        id: uiPrefs
        category: "ui"
        property bool remindersTipHidden: false
    }

    function confirmItem(occurrenceId) {
        ReminderVM.confirm(occurrenceId)
    }

    function snoozeItem(occurrenceId) {
        ReminderVM.snooze(occurrenceId)
    }

    // 撤销 = 回到"已提醒未确认"；撤销条里再点一次就重新确认。
    function reopenItem(occurrenceId) {
        ReminderVM.undo(occurrenceId)
        Undo.show("已恢复为待确认", function () {
            ReminderVM.confirm(occurrenceId)
        })
    }

    function pick(kind) {
        var list = ReminderVM.today
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var st = list[i].state
            var k = st === "confirmed" ? "done" : st === "missed" ? "missed" : "todo"
            if (k === kind)
                out.push(list[i])
        }
        return out
    }

    readonly property var todoList: pick("todo")
    readonly property var missedList: pick("missed")
    readonly property var doneList: pick("done")
    property bool upcomingOpen: false

    function detailText(item) {
        var parts = []
        if (item.dose)
            parts.push(item.dose)
        if (item.note)
            parts.push(item.note)
        return parts.join("\n")
    }

    // 卡片右上角的小“管理”胶囊：比整颗大按钮省地方，点击区仍有触控尺寸。
    component ManagePill: Rectangle {
        id: pill
        signal clicked()
        implicitWidth: pillRow.implicitWidth + page.theme.px(20)
        implicitHeight: page.theme.px(44)
        radius: page.theme.radiusSmall
        color: page.theme.surfaceAlt

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: page.theme.px(4)

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "more"
                size: page.theme.px(20)
                strokeWidth: 3
                color: page.theme.inkSoft
            }

            AppText {
                anchors.verticalCenter: parent.verticalCenter
                theme: page.theme
                role: "label"
                font.pixelSize: page.theme.px(19)
                color: page.theme.inkSoft
                text: "管理"
                wrapMode: Text.NoWrap
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -page.theme.px(4)
            onClicked: pill.clicked()
        }

        Accessible.role: Accessible.Button
        Accessible.name: "管理这条提醒"
    }

    component SectionTitle: AppText {
        property int count: 0
        property string label: ""
        width: list.width
        topPadding: page.theme.px(6)
        theme: page.theme
        role: "label"
        font.pixelSize: page.theme.px(22)
        font.weight: Font.Bold
        text: label + " · " + count
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
            title: "今天的提醒"
            subtitle: ReminderVM.todayTotal > 0
                ? "做完 " + ReminderVM.todayDone + " / " + ReminderVM.todayTotal
                : "还没有提醒"
            showBack: true
            actionText: "新建"
            onBack: page.back()
            onAction: page.addRequested()
        }

        Flickable {
            id: scroll
            objectName: "remindersScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: list.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: list
                x: page.theme.pageMargin
                width: scroll.width - page.theme.pageMargin * 2
                spacing: page.theme.px(12)
                topPadding: page.theme.px(4)
                bottomPadding: page.theme.gap

                // 一行小提示，可以关掉（原来是每次都占三行的说明卡）。
                Rectangle {
                    objectName: "remindersTip"
                    width: list.width
                    visible: !uiPrefs.remindersTipHidden
                    height: tipRow.implicitHeight + page.theme.px(12)
                    radius: page.theme.radiusSmall
                    color: page.theme.primarySoft

                    RowLayout {
                        id: tipRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: page.theme.px(14)
                        anchors.rightMargin: page.theme.px(2)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: page.theme.px(10)

                        Icon {
                            Layout.alignment: Qt.AlignVCenter
                            name: "info"
                            size: page.theme.px(22)
                            color: page.theme.primaryInk
                        }

                        AppText {
                            Layout.fillWidth: true
                            theme: page.theme
                            role: "label"
                            font.pixelSize: page.theme.px(19)
                            color: page.theme.primaryInk
                            text: "到点会全屏提醒，按错了能撤销，每条最多推迟 3 次"
                            wrapMode: Text.WordWrap
                            lineHeight: 1.0
                        }

                        Item {
                            Layout.preferredWidth: page.theme.touch
                            Layout.preferredHeight: page.theme.touch

                            Icon {
                                anchors.centerIn: parent
                                name: "close"
                                size: page.theme.px(20)
                                strokeWidth: 2.6
                                color: page.theme.primaryInk
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: uiPrefs.remindersTipHidden = true
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: "关闭提示"
                        }
                    }
                }

                // ------------------------------------------------ 待完成
                SectionTitle {
                    visible: page.todoList.length > 0
                    label: "待完成"
                    count: page.todoList.length
                    color: page.theme.amber
                }

                Repeater {
                    model: page.todoList

                    delegate: Card {
                        id: card
                        required property var modelData

                        // notified 也要可操作：确认后撤销会回到 notified，若不算 actionable，两个按钮都会变灰。
                        readonly property bool actionable: modelData.state === "pending"
                            || modelData.state === "notified"
                            || modelData.state === "snoozed"
                        readonly property bool isEscalated: modelData.state === "escalated"
                        readonly property bool canComplete: modelData.canConfirm === true
                        readonly property bool strict: modelData.allowLate === false
                        // 到点了没有：推迟中看推迟到的时刻，否则看原定时刻。
                        readonly property double targetTs: modelData.state === "snoozed"
                            && Number(modelData.snoozeUntilTs || 0) > 0
                            ? Number(modelData.snoozeUntilTs) : Number(modelData.dueTs)
                        readonly property bool isDue: ReminderVM.nowMs >= targetTs

                        objectName: "todoCard"
                        width: list.width
                        height: cardColumn.implicitHeight + page.theme.px(18) * 2
                        theme: page.theme
                        border.width: isDue ? 2 : 1
                        border.color: isDue ? page.theme.amberBar : page.theme.cardLine

                        Column {
                            id: cardColumn
                            width: parent.width - page.theme.px(18) * 2
                            x: page.theme.px(18)
                            y: page.theme.px(18)
                            spacing: page.theme.px(10)

                            RowLayout {
                                width: parent.width
                                spacing: page.theme.gapSmall

                                AppText {
                                    theme: page.theme
                                    role: "body"
                                    font.weight: Font.Bold
                                    text: Fmt.hhmm(card.modelData.dueTs)
                                    wrapMode: Text.NoWrap
                                }

                                Rectangle {
                                    Layout.preferredHeight: todoChip.implicitHeight + page.theme.px(6)
                                    // 空间不够时先让状态标签收窄（文字省略），时间和“管理”不动。
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: page.theme.px(60)
                                    Layout.preferredWidth: todoChip.implicitWidth + page.theme.px(20)
                                    Layout.maximumWidth: todoChip.implicitWidth + page.theme.px(20)
                                    radius: height / 2
                                    color: card.isEscalated ? page.theme.dangerSoft
                                        : card.isDue ? page.theme.amberSoft
                                        : page.theme.surfaceAlt

                                    AppText {
                                        id: todoChip
                                        anchors.centerIn: parent
                                        width: Math.min(implicitWidth, parent.width - page.theme.px(16))
                                        theme: page.theme
                                        role: "label"
                                        font.pixelSize: page.theme.px(18)
                                        font.weight: Font.Bold
                                        color: card.isEscalated ? page.theme.dangerInk
                                            : card.isDue ? page.theme.amberInk
                                            : page.theme.inkSoft
                                        wrapMode: Text.NoWrap
                                        elide: Text.ElideRight
                                        text: card.isEscalated ? "已通知家人"
                                            : card.isDue ? Fmt.agoText((ReminderVM.nowMs - card.targetTs) / 60000)
                                            : card.modelData.state === "snoozed" ? "推迟到 " + Fmt.hhmm(card.targetTs)
                                            : Fmt.countdownText(Math.ceil((card.targetTs - ReminderVM.nowMs) / 60000))
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            // 事项名和“管理”同一行：名字长了自己换行，状态标签那一行就不挤了。
                            RowLayout {
                                width: parent.width
                                spacing: page.theme.gapSmall

                                AppText {
                                    Layout.fillWidth: true
                                    theme: page.theme
                                    role: "title"
                                    font.pixelSize: page.theme.px(34)
                                    font.weight: Font.Bold
                                    text: card.modelData.title
                                    wrapMode: Text.WordWrap
                                }

                                ManagePill {
                                    Layout.alignment: Qt.AlignTop
                                    onClicked: manageSheet.openFor(card.modelData)
                                }
                            }

                            AppText {
                                width: parent.width
                                visible: text !== ""
                                theme: page.theme
                                role: "body"
                                font.pixelSize: page.theme.px(22)
                                color: page.theme.inkSoft
                                text: page.detailText(card.modelData)
                                wrapMode: Text.WordWrap
                                lineHeight: 1.05
                            }

                            RowLayout {
                                width: parent.width
                                spacing: page.theme.gapSmall

                                AppButton {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.button
                                    theme: page.theme
                                    text: "完成"
                                    iconName: "check"
                                    kind: "primary"
                                    enabled: card.canComplete
                                    onClicked: page.confirmItem(card.modelData.occurrenceId)
                                }

                                AppButton {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: page.theme.button
                                    theme: page.theme
                                    text: card.isEscalated ? "已通知家人" : "推迟"
                                    subtitle: card.isEscalated ? "" : "10 分钟"
                                    kind: "secondary"
                                    enabled: card.actionable && card.modelData.canSnooze !== false
                                    onClicked: page.snoozeItem(card.modelData.occurrenceId)
                                }
                            }

                            AppText {
                                width: parent.width
                                visible: card.strict
                                theme: page.theme
                                role: "label"
                                font.pixelSize: page.theme.px(19)
                                color: page.theme.inkSoft
                                text: "需按时完成，错过后不能补做"
                                wrapMode: Text.WordWrap
                            }

                            AppText {
                                width: parent.width
                                visible: card.actionable && card.modelData.snoozeCount >= 3
                                theme: page.theme
                                role: "label"
                                font.pixelSize: page.theme.px(19)
                                color: page.theme.danger
                                text: "已推迟 3 次，请先完成或联系家人"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                // ------------------------------------------------ 已错过
                SectionTitle {
                    visible: page.missedList.length > 0
                    label: "已错过"
                    count: page.missedList.length
                    color: page.theme.danger
                }

                Repeater {
                    model: page.missedList

                    delegate: Card {
                        id: missedCard
                        required property var modelData
                        // 能不能补做由 core 决定：只有"允许过时补做"的计划才能补点。
                        readonly property bool canMakeUp: modelData.canConfirm === true

                        objectName: "missedCard"
                        width: list.width
                        height: missedColumn.implicitHeight + page.theme.px(14) * 2
                        theme: page.theme

                        Column {
                            id: missedColumn
                            width: parent.width - page.theme.px(18) * 2
                            x: page.theme.px(18)
                            y: page.theme.px(14)
                            spacing: page.theme.px(6)

                            RowLayout {
                                width: parent.width
                                spacing: page.theme.gapSmall

                                Column {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    AppText {
                                        width: parent.width
                                        theme: page.theme
                                        role: "label"
                                        font.weight: Font.Bold
                                        color: page.theme.inkSoft
                                        text: Fmt.hhmm(missedCard.modelData.dueTs)
                                        wrapMode: Text.NoWrap
                                    }

                                    AppText {
                                        width: parent.width
                                        theme: page.theme
                                        role: "heading"
                                        font.weight: Font.Bold
                                        text: missedCard.modelData.title
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                ManagePill {
                                    Layout.alignment: Qt.AlignTop
                                    onClicked: manageSheet.openFor(missedCard.modelData)
                                }
                            }

                            AppText {
                                width: parent.width
                                theme: page.theme
                                role: "label"
                                font.pixelSize: page.theme.px(19)
                                color: missedCard.canMakeUp ? page.theme.inkSoft : page.theme.dangerInk
                                text: missedCard.canMakeUp
                                    ? "过了时间，还可以补做"
                                    : "这条要求按时完成，过了时间不能补做"
                                wrapMode: Text.WordWrap
                            }

                            AppButton {
                                visible: missedCard.canMakeUp
                                width: parent.width
                                height: page.theme.touch
                                theme: page.theme
                                text: "补做完成"
                                iconName: "check"
                                kind: "secondary"
                                onClicked: page.confirmItem(missedCard.modelData.occurrenceId)
                            }
                        }
                    }
                }

                // ------------------------------------------------ 已完成：一行一条
                SectionTitle {
                    visible: page.doneList.length > 0
                    label: "已完成"
                    count: page.doneList.length
                    color: page.theme.primary
                }

                Card {
                    objectName: "doneCard"
                    width: list.width
                    visible: page.doneList.length > 0
                    height: doneColumn.implicitHeight + page.theme.px(4) * 2
                    theme: page.theme

                    Column {
                        id: doneColumn
                        width: parent.width - page.theme.px(16) - page.theme.px(6)
                        x: page.theme.px(16)
                        y: page.theme.px(4)

                        Repeater {
                            model: page.doneList

                            delegate: Item {
                                id: doneRow
                                required property var modelData
                                required property int index

                                width: doneColumn.width
                                height: Math.max(page.theme.touch, doneTitle.implicitHeight + page.theme.px(44))

                                Rectangle {
                                    visible: doneRow.index > 0
                                    width: parent.width
                                    height: 1
                                    color: page.theme.cardLine
                                }

                                // 点左边整块打开管理面板（删除、改时间等），右边是撤销。
                                MouseArea {
                                    anchors.left: parent.left
                                    anchors.right: undoButton.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    onClicked: manageSheet.openFor(doneRow.modelData)
                                    Accessible.role: Accessible.Button
                                    Accessible.name: "管理：" + doneRow.modelData.title
                                }

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: undoButton.left
                                    anchors.rightMargin: page.theme.px(6)
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: page.theme.px(10)

                                    Icon {
                                        name: "check"
                                        size: page.theme.px(26)
                                        strokeWidth: 3
                                        color: page.theme.primary
                                    }

                                    // 名字在上、时间在下：一行留给名字，不再被挤成“晨起喝/水”。
                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 0

                                        AppText {
                                            id: doneTitle
                                            width: parent.width
                                            theme: page.theme
                                            role: "body"
                                            text: doneRow.modelData.title
                                            wrapMode: Text.WordWrap
                                        }

                                        AppText {
                                            width: parent.width
                                            theme: page.theme
                                            role: "label"
                                            font.pixelSize: page.theme.px(18)
                                            color: page.theme.inkSoft
                                            text: Fmt.hhmm(doneRow.modelData.dueTs)
                                            wrapMode: Text.NoWrap
                                        }
                                    }
                                }

                                Rectangle {
                                    id: undoButton
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: page.theme.px(48)
                                    width: undoRow.implicitWidth + page.theme.px(20)
                                    radius: page.theme.radiusSmall
                                    color: undoMouse.pressed ? page.theme.primarySoft : "transparent"

                                    Row {
                                        id: undoRow
                                        anchors.centerIn: parent
                                        spacing: page.theme.px(4)

                                        Icon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "undo"
                                            size: page.theme.px(22)
                                            strokeWidth: 2.6
                                            color: page.theme.primary
                                        }

                                        AppText {
                                            anchors.verticalCenter: parent.verticalCenter
                                            theme: page.theme
                                            role: "label"
                                            font.weight: Font.Bold
                                            color: page.theme.primary
                                            text: "撤销"
                                            wrapMode: Text.NoWrap
                                        }
                                    }

                                    MouseArea {
                                        id: undoMouse
                                        anchors.fill: parent
                                        anchors.margins: -page.theme.px(6)
                                        onClicked: page.reopenItem(doneRow.modelData.occurrenceId)
                                    }

                                    Accessible.role: Accessible.Button
                                    Accessible.name: "撤销：" + doneRow.modelData.title
                                }
                            }
                        }
                    }
                }

                // ------------------------------------------------ 空状态
                Card {
                    width: list.width
                    height: emptyColumn.implicitHeight + page.theme.gap * 2
                    theme: page.theme
                    visible: ReminderVM.today.length === 0

                    Column {
                        id: emptyColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.gap
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "heading"
                            text: "今天还没有提醒"
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.inkSoft
                            text: "可以点右上角“新建”，加一条吃药或喝水提醒。"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // ------------------------------------------------ 接下来（明天起），默认折叠成一行
                Rectangle {
                    objectName: "upcomingCard"
                    width: list.width
                    visible: ReminderVM.upcoming.length > 0
                    height: upcomingColumn.implicitHeight
                    radius: page.theme.radius
                    color: page.theme.surfaceAlt

                    Column {
                        id: upcomingColumn
                        width: parent.width

                        Item {
                            width: parent.width
                            height: Math.max(page.theme.touch, upHead.implicitHeight + page.theme.px(20))

                            RowLayout {
                                id: upHead
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: page.theme.px(18)
                                anchors.rightMargin: page.theme.px(14)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: page.theme.gapSmall

                                AppText {
                                    theme: page.theme
                                    role: "body"
                                    font.weight: Font.Bold
                                    text: ReminderVM.upcoming.length > 0
                                        ? ReminderVM.upcoming[0].dayLabel + " " + ReminderVM.upcoming.length + " 条"
                                        : ""
                                    wrapMode: Text.NoWrap
                                }

                                AppText {
                                    Layout.fillWidth: true
                                    theme: page.theme
                                    role: "label"
                                    font.pixelSize: page.theme.px(19)
                                    color: page.theme.inkSoft
                                    text: ReminderVM.upcoming.length > 0
                                        ? Fmt.hhmm(ReminderVM.upcoming[0].dueTs) + " 起"
                                        : ""
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                }

                                Icon {
                                    name: page.upcomingOpen ? "chevronUp" : "chevronDown"
                                    size: page.theme.px(24)
                                    strokeWidth: 2.6
                                    color: page.theme.inkSoft
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: page.upcomingOpen = !page.upcomingOpen
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: page.upcomingOpen ? "收起接下来的提醒" : "展开接下来的提醒"
                        }

                        Column {
                            visible: page.upcomingOpen
                            x: page.theme.px(18)
                            width: parent.width - page.theme.px(36)
                            spacing: page.theme.px(8)
                            bottomPadding: page.theme.px(16)

                            Repeater {
                                model: ReminderVM.upcoming

                                // 标题允许换行，不再截成“晨起···”。
                                delegate: RowLayout {
                                    id: upRow
                                    required property var modelData
                                    width: parent.width
                                    spacing: page.theme.gapSmall

                                    AppText {
                                        Layout.alignment: Qt.AlignTop
                                        theme: page.theme
                                        role: "label"
                                        font.weight: Font.Bold
                                        color: page.theme.inkSoft
                                        text: (upRow.modelData.dayLabel !== ReminderVM.upcoming[0].dayLabel
                                               ? upRow.modelData.dayLabel + " " : "")
                                            + Fmt.hhmm(upRow.modelData.dueTs)
                                        wrapMode: Text.NoWrap
                                        topPadding: page.theme.px(3)
                                    }

                                    AppText {
                                        Layout.fillWidth: true
                                        theme: page.theme
                                        role: "body"
                                        text: upRow.modelData.title
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    PlanManageSheet {
        id: manageSheet
        objectName: "manageSheet"
        theme: page.theme
    }
}
