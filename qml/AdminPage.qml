pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 看护人管理后台：紧急联系人、服务器地址、同步状态。
// 只能从 CaregiverGatePage 验证成功后进入。
Item {
    id: page

    property Theme theme

    signal back()

    property string contactError: ""
    property string networkError: ""

    function saveContact() {
        const name = contactName.text.trim()
        const phone = contactPhone.text.trim()
        const phonePattern = /^[0-9 +()-]{7,20}$/

        if (name.length === 0) {
            page.contactError = "请填写联系人姓名"
            contactName.forceFocus()
            return
        }
        if (!phonePattern.test(phone)) {
            page.contactError = "请填写 7–20 位电话号码，可带空格、+、-"
            contactPhone.forceFocus()
            return
        }

        if (ProfileVM.updateCaregiver(name, phone)) {
            page.contactError = ""
            Notify.info("紧急联系人已保存")
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
                        }

                        Field {
                            id: contactPhone
                            width: parent.width
                            theme: page.theme
                            label: "电话"
                            placeholder: "例如：138 0000 5678"
                            text: ProfileVM.caregiverPhone
                            maxLength: 20
                            inputMethodHints: Qt.ImhDialableCharactersOnly
                        }

                        AppText {
                            width: parent.width
                            visible: page.contactError.length > 0
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

                        RowLayout {
                            width: parent.width

                            AppText {
                                theme: page.theme
                                role: "body"
                                text: "Wi-Fi"
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                theme: page.theme
                                role: "label"
                                color: DeviceVM.networkOnline ? page.theme.primary : page.theme.danger
                                font.weight: Font.DemiBold
                                text: DeviceVM.networkOnline ? DeviceVM.wifiName + " · 已连接" : "未连接"
                            }
                        }

                        Field {
                            id: serverUrl
                            width: parent.width
                            theme: page.theme
                            label: "服务器地址"
                            placeholder: "https://..."
                            text: DeviceVM.serverUrl
                            fontSize: page.theme.px(20)
                            maxLength: 80
                            inputMethodHints: Qt.ImhUrlCharactersOnly
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

                        AppButton {
                            width: parent.width
                            height: page.theme.touch
                            theme: page.theme
                            text: "测试连接"
                            kind: "secondary"
                            onClicked: Notify.info("网络连接正常（演示版）")
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
}
