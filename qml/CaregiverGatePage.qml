pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 看护人缓冲页：必须输入“我是看护人”才能进入管理后台。
// 60 秒内没有完成验证，直接退回主界面，避免老人误入后被困住。
Item {
    id: page

    property Theme theme

    signal unlocked()
    signal cancelled()

    property int secondsLeft: 60
    property bool finished: false
    property string errorText: ""

    function checkPhrase() {
        if (page.finished)
            return
        if (input.text.trim() === "我是看护人") {
            page.finish(true)
        } else {
            page.errorText = "输入不对，请照着上面的五个字再输一次"
            input.selectAll()
            input.forceActiveFocus()
        }
    }

    function finish(success) {
        if (page.finished)
            return
        page.finished = true
        countdown.stop()
        if (success)
            page.unlocked()
        else
            page.cancelled()
    }

    Component.onCompleted: input.forceActiveFocus()

    Timer {
        id: countdown
        interval: 1000
        repeat: true
        running: !page.finished
        onTriggered: {
            page.secondsLeft -= 1
            if (page.secondsLeft <= 0) {
                page.secondsLeft = 0
                page.finish(false)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: page.theme.pageMargin
        anchors.rightMargin: page.theme.pageMargin
        anchors.topMargin: page.theme.gapSmall
        anchors.bottomMargin: page.theme.gap
        spacing: page.theme.gap

        RowLayout {
            Layout.fillWidth: true

            AppText {
                theme: page.theme
                role: "body"
                font.weight: Font.DemiBold
                text: "看护人验证"
            }

            Item { Layout.fillWidth: true }

            AppButton {
                Layout.preferredWidth: page.theme.px(96)
                Layout.preferredHeight: page.theme.touch
                theme: page.theme
                text: "退出"
                kind: "secondary"
                onClicked: page.finish(false)
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: body
                width: parent.width
                spacing: page.theme.gap

                AppText {
                    width: parent.width
                    theme: page.theme
                    role: "body"
                    color: page.theme.inkSoft
                    text: "这里不是给老人用的。请照下面的五个字输入，确认您是看护人。"
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    height: phraseColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radius
                    color: page.theme.surface
                    border.width: 3
                    border.color: page.theme.primary

                    Column {
                        id: phraseColumn
                        width: parent.width - page.theme.gap * 2
                        anchors.centerIn: parent
                        spacing: page.theme.px(6)

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "label"
                            color: page.theme.inkSoft
                            text: "请照抄这五个字"
                        }

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "title"
                            color: page.theme.primaryInk
                            text: "我是看护人"
                            wrapMode: Text.NoWrap
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: page.theme.px(92)
                    radius: page.theme.radiusSmall
                    color: page.theme.surface
                    border.width: input.activeFocus ? 3 : 2
                    border.color: input.activeFocus ? page.theme.primary : page.theme.line

                    AppText {
                        anchors.centerIn: parent
                        visible: input.text.length === 0
                        theme: page.theme
                        role: "body"
                        color: page.theme.inkSoft
                        opacity: 0.6
                        text: "在这里输入"
                    }

                    TextInput {
                        id: input
                        objectName: "phraseInput"
                        anchors.fill: parent
                        anchors.leftMargin: page.theme.px(16)
                        anchors.rightMargin: page.theme.px(16)
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        font.family: page.theme.fontFamily
                        font.pixelSize: page.theme.px(30)
                        color: page.theme.ink
                        selectionColor: page.theme.primary
                        selectedTextColor: "#FFFFFF"
                        maximumLength: 20
                        clip: true
                        onAccepted: page.checkPhrase()
                        onTextChanged: page.errorText = ""
                    }
                }

                AppText {
                    width: parent.width
                    visible: page.errorText.length > 0
                    theme: page.theme
                    role: "body"
                    color: page.theme.danger
                    text: page.errorText
                    wrapMode: Text.WordWrap
                }

                AppButton {
                    width: parent.width
                    height: page.theme.buttonLarge
                    theme: page.theme
                    text: "确认进入管理后台"
                    kind: "primary"
                    onClicked: page.checkPhrase()
                }

                Rectangle {
                    width: parent.width
                    height: timeoutColumn.implicitHeight + page.theme.gap * 1.5
                    radius: page.theme.radiusSmall
                    color: page.theme.surfaceAlt
                    border.width: 2
                    border.color: page.theme.line

                    Column {
                        id: timeoutColumn
                        width: parent.width - page.theme.gap * 2
                        anchors.centerIn: parent
                        spacing: page.theme.px(8)

                        AppText {
                            width: parent.width
                            theme: page.theme
                            role: "body"
                            color: page.theme.amberInk
                            text: "还剩 " + page.secondsLeft + " 秒，超时会自动返回主界面"
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            width: parent.width
                            height: page.theme.px(10)
                            radius: height / 2
                            color: page.theme.line

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, page.secondsLeft / 60))
                                height: parent.height
                                radius: height / 2
                                color: page.theme.amber
                            }
                        }
                    }
                }
            }
        }

        AppButton {
            Layout.fillWidth: true
            Layout.preferredHeight: page.theme.buttonLarge
            theme: page.theme
            text: "退出，返回主界面"
            kind: "secondary"
            onClicked: page.finish(false)
        }
    }
}
