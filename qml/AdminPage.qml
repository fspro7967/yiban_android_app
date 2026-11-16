pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 看护人管理后台：紧急联系人、服务器地址、同步状态、外设板、AI 助手、操作日志。
// 只能从 CaregiverGatePage 验证成功后进入。
Item {
    id: page
    objectName: "adminPage"

    property Theme theme

    signal back()

    property string contactError: ""
    property string networkError: ""
    // 测试连接的结果只属于当前填的这个地址：地址一改就清掉，避免拿旧结论对新地址。
    property string probeText: ""
    property bool probeOk: false
    // 电话格式问题：边输边查，但只在用户点过保存或输入框失焦后才标红，避免一敲就报错。
    property bool phoneTouched: false
    readonly property string phoneProblem: ProfileVM.phoneProblem(contactPhone.text)
    readonly property bool nameMissing: contactName.text.trim().length === 0

    // 操作日志一次只画最近的 logPageSize 条，点"查看更早的"再往下加，避免一页画几百行。
    readonly property int logPageSize: 20
    property int logShown: logPageSize

    // 进出后台都留痕。退出用 StackView 的移除通知：页头返回、底部按钮、系统返回键都会走到。
    Component.onCompleted: LogVM.note("管理", "进入管理后台")
    StackView.onRemoved: LogVM.note("管理", "退出管理后台")

    function saveContact() {
        const name = contactName.text.trim()
        const phone = contactPhone.text.trim()
        page.phoneTouched = true

        if (name.length === 0) {
            page.contactError = "请填写联系人姓名"
            contactName.forceFocus()
            return
        }
        if (page.phoneProblem.length > 0) {
            page.contactError = page.phoneProblem
            contactPhone.forceFocus()
            return
        }

        if (ProfileVM.updateCaregiver(name, phone)) {
            page.contactError = ""
            page.phoneTouched = false
            // 保存后的号码已统一成"138 0000 5678"格式，输入框跟着刷新。
            contactPhone.text = ProfileVM.caregiverPhone
            Notify.info("紧急联系人已保存：" + ProfileVM.caregiverPhone)
        } else {
            page.contactError = "保存失败，请检查姓名和电话"
        }
    }

    function saveNetwork() {
        if (DeviceVM.saveNetwork(serverUrl.text)) {
            page.networkError = ""
        } else {
            page.networkError = "服务器地址要以 https:// 开头"
        }
    }

    Connections {
        target: DeviceVM

        function onConnectionTested(ok, message) {
            page.probeOk = ok
            page.probeText = message
        }
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
            title: "管理后台"
            subtitle: "仅看护人使用"
            showBack: true
            onBack: page.back()
        }

        Flickable {
            objectName: "adminScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: page.theme.pageMargin
            Layout.rightMargin: page.theme.pageMargin
            contentWidth: width
            contentHeight: adminList.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: adminList
                width: parent.width
                spacing: page.theme.gap
                topPadding: page.theme.gapSmall
                bottomPadding: page.theme.gap * 2

                Rectangle {
                    width: parent.width
                    height: warningText.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radiusSmall
                    color: page.theme.amberSoft
                    border.width: 2
                    border.color: page.theme.amber

                    AppText {
                        id: warningText
                        width: parent.width - page.theme.gap * 2
                        anchors.centerIn: parent
                        theme: page.theme
                        role: "label"
                        color: page.theme.amberInk
                        text: "这些设置会影响提醒、呼救和同步，请确认您是看护人。"
                        wrapMode: Text.WordWrap
                    }
                }

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "紧急联系人"
                }

                Rectangle {
                    width: parent.width
                    height: contactColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: contactColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            text: "到点未确认时，会通知这位家人"
                            wrapMode: Text.WordWrap
                        }

                        Field {
                            id: contactName
                            width: parent.width
                            theme: page.theme
                            label: "姓名"
                            placeholder: "例如：女儿 陈晓"
                            text: ProfileVM.caregiverName
                            maxLength: 24
                            invalid: page.contactError.length > 0 && page.nameMissing
                            errorText: "请填写联系人姓名"
                            onTextChanged: if (!page.nameMissing && page.contactError === "请填写联系人姓名") page.contactError = ""
                        }

                        Field {
                            id: contactPhone
                            objectName: "contactPhone"
                            width: parent.width
                            theme: page.theme
                            label: "电话"
                            placeholder: "例如：138 0000 5678"
                            text: ProfileVM.caregiverPhone
                            maxLength: 20
                            inputMethodHints: Qt.ImhDialableCharactersOnly
                            invalid: page.phoneTouched && page.phoneProblem.length > 0
                            errorText: page.phoneProblem
                            onEditingFinished: page.phoneTouched = true
                            onTextChanged: {
                                if (page.phoneProblem.length === 0 && page.contactError.length > 0 && !page.nameMissing)
                                    page.contactError = ""
                            }
                        }

                        AppText {
                            width: parent.width
                            visible: page.phoneProblem.length === 0 && contactPhone.text.trim().length > 0
                                && ProfileVM.formatPhone(contactPhone.text) !== contactPhone.text.trim()
                            theme: page.theme
                            role: "label"
                            color: page.theme.primary
                            text: "✓ 号码正确，保存为 " + ProfileVM.formatPhone(contactPhone.text)
                            wrapMode: Text.WordWrap
                        }

                        AppText {
                            width: parent.width
                            // 字段自己已经标红显示原因时，这里不再重复同一句。
                            visible: page.contactError.length > 0 && !contactName.invalid && !contactPhone.invalid
                            theme: page.theme
                            role: "label"
                            color: page.theme.danger
                            text: page.contactError
                            wrapMode: Text.WordWrap
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "保存联系人"
                            kind: "primary"
                            onClicked: page.saveContact()
                        }
                    }
                }

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "网络"
                }

                Rectangle {
                    width: parent.width
                    height: networkColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: networkColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        // 手机自己的联网状态（系统给的），和下面"测试连接"查的服务器是否可达是两件事。
                        RowLayout {
                            width: parent.width
                            spacing: page.theme.gapSmall

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "手机联网"
                            }

                            AppText {
                                objectName: "networkState"
                                Layout.fillWidth: true
                                theme: page.theme
                                role: "label"
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.Wrap
                                font.weight: Font.DemiBold
                                color: DeviceVM.networkStatus === "online" ? page.theme.primary
                                    : DeviceVM.networkStatus === "offline" ? page.theme.danger
                                    : DeviceVM.networkStatus === "unknown" ? page.theme.inkSoft
                                    : page.theme.amberInk
                                text: {
                                    var type = DeviceVM.networkType
                                    var prefix = type.length > 0 ? type + " · " : ""
                                    switch (DeviceVM.networkStatus) {
                                    case "online": return prefix + "已联网"
                                    case "limited": return prefix + "已连接，但上不了外网"
                                    case "portal": return prefix + "需要先在网页上登录"
                                    case "offline": return "未联网"
                                    default: return "读不到联网状态"
                                    }
                                }
                            }
                        }

                        Field {
                            id: serverUrl
                            objectName: "serverUrl"
                            width: parent.width
                            theme: page.theme
                            label: "服务器地址"
                            placeholder: "https://..."
                            text: DeviceVM.serverUrl
                            fontSize: page.theme.px(20)
                            maxLength: 80
                            inputMethodHints: Qt.ImhUrlCharactersOnly
                            onTextChanged: page.probeText = ""
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "演示版只保存在内存里，重启后恢复默认地址。"
                            wrapMode: Text.WordWrap
                        }

                        AppText {
                            width: parent.width
                            visible: page.networkError.length > 0
                            theme: page.theme
                            role: "label"
                            color: page.theme.danger
                            text: page.networkError
                            wrapMode: Text.WordWrap
                        }

                        AppText {
                            objectName: "probeResult"
                            width: parent.width
                            visible: page.probeText.length > 0 && !DeviceVM.testingConnection
                            theme: page.theme
                            role: "label"
                            color: page.probeOk ? page.theme.primary : page.theme.danger
                            text: (page.probeOk ? "✓ " : "✗ ") + page.probeText
                            wrapMode: Text.Wrap
                        }

                        AppButton {
                            objectName: "probeButton"
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: DeviceVM.testingConnection ? "正在测试…" : "测试连接"
                            kind: "secondary"
                            enabled: !DeviceVM.testingConnection
                            onClicked: {
                                page.probeText = ""
                                DeviceVM.testConnection(serverUrl.text)
                            }
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "保存网络设置"
                            kind: "primary"
                            onClicked: page.saveNetwork()
                        }
                    }
                }

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "同步"
                }

                Rectangle {
                    width: parent.width
                    height: syncColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: syncColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "同步状态"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: SyncVM.syncState === "offline" ? page.theme.amberInk : page.theme.primary
                                font.weight: Font.DemiBold
                                text: SyncVM.syncState === "offline" ? "离线（演示）" : SyncVM.syncState
                            }
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "待上传 " + SyncVM.pendingCount + " 条记录；提醒和记录在离线时仍可使用。"
                            wrapMode: Text.WordWrap
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "立即同步"
                            kind: "secondary"
                            onClicked: SyncVM.retrySync()
                        }
                    }
                }

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "外设板"
                }

                BoardCard {
                    width: parent.width
                    theme: page.theme
                }

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "AI 助手"
                }

                AssistantCard {
                    width: parent.width
                    theme: page.theme
                }

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "label"
                    color: page.theme.inkSoft
                    text: "操作日志"
                }

                Rectangle {
                    objectName: "logCard"
                    width: parent.width
                    height: logColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: logColumn
                        width: parent.width - page.theme.gap * 2
                        x: page.theme.gap
                        y: page.theme.px(12)
                        spacing: page.theme.gapSmall

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            text: "共 " + LogVM.count + " 条，最新的在最上面"
                            wrapMode: Text.WordWrap
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "提醒、计划、记录、设置的每一步都会记在这里。演示版只保存在内存里，最多 200 条，重启后清空。"
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: LogVM.entries.slice(0, page.logShown)

                            delegate: Column {
                                id: logRow

                                required property var modelData
                                required property int index

                                width: logColumn.width
                                spacing: page.theme.px(4)

                                Rectangle {
                                    width: parent.width
                                    height: 2
                                    color: page.theme.line
                                }

                                Row {
                                    width: parent.width
                                    topPadding: page.theme.px(6)
                                    spacing: page.theme.gapSmall

                                    Rectangle {
                                        id: categoryChip
                                        anchors.verticalCenter: logTimeText.verticalCenter
                                        width: categoryText.implicitWidth + page.theme.px(16)
                                        height: categoryText.implicitHeight + page.theme.px(6)
                                        radius: height / 2
                                        color: logRow.modelData.category === "呼救" ? page.theme.dangerSoft
                                            : logRow.modelData.category === "管理" ? page.theme.amberSoft
                                            : page.theme.primarySoft

                                        AppText {
                                            id: categoryText
                                            anchors.centerIn: parent
                                            theme: page.theme
                                            font.pixelSize: page.theme.fontChip
                                            font.weight: Font.DemiBold
                                            color: logRow.modelData.category === "呼救" ? page.theme.dangerInk
                                                : logRow.modelData.category === "管理" ? page.theme.amberInk
                                                : page.theme.primaryInk
                                            text: logRow.modelData.category
                                        }
                                    }

                                    AppText {
                                        id: logTimeText
                                        theme: page.theme
                                        role: "label"
                                        color: page.theme.inkSoft
                                        text: Fmt.logTime(logRow.modelData.ts)
                                    }
                                }

                                AppText {
                                    width: parent.width
                                    theme: page.theme
                                    role: "label"
                                    text: logRow.modelData.text
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        AppButton {
                            objectName: "logMore"
                            width: parent.width
                            height: page.theme.touch
                            visible: LogVM.count > page.logShown
                            theme: page.theme
                            text: "查看更早的 " + Math.min(page.logPageSize, LogVM.count - page.logShown) + " 条"
                            kind: "secondary"
                            onClicked: page.logShown += page.logPageSize
                        }

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            // 清空后还剩一条"清空了操作日志"，这时再清没有意义。
                            enabled: LogVM.count > 1
                            theme: page.theme
                            text: "清空日志"
                            kind: "secondary"
                            onClicked: clearLogDialog.open()
                        }
                    }
                }

                AppButton {
                    width: parent.width
                    height: page.theme.buttonLarge
                    theme: page.theme
                    text: "退出管理后台"
                    kind: "secondary"
                    onClicked: page.back()
                }
            }
        }
    }

    ConfirmDialog {
        id: clearLogDialog
        theme: page.theme
        title: "清空操作日志"
        message: "现有的 " + LogVM.count + " 条日志会被删掉，不能恢复。"
        confirmText: "清空"
        cancelText: "取消"
        danger: true
        onAccepted: {
            LogVM.clear()
            page.logShown = page.logPageSize
        }
    }
}
