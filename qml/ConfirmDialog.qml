pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 关键操作二次确认。默认焦点在“取消”上，避免顺手点错。
Item {
    id: dialog

    property Theme theme
    property string title: ""
    property string message: ""
    property string confirmText: "确认"
    property string cancelText: "取消"
    property bool danger: false

    signal accepted()
    signal cancelled()

    anchors.fill: parent
    visible: false
    z: 200

    function open() {
        visible = true
    }

    function close() {
        visible = false
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
        width: Math.min(parent.width - dialog.theme.px(40), dialog.theme.px(440))
        height: content.implicitHeight + dialog.theme.px(48)
        anchors.centerIn: parent
        radius: dialog.theme.radius
        color: dialog.theme.surface
        border.width: 2
        border.color: dialog.theme.line

        ColumnLayout {
            id: content
            width: card.width - dialog.theme.px(40)
            anchors.centerIn: parent
            spacing: dialog.theme.gap

            AppText {
                Layout.fillWidth: true
                theme: dialog.theme
                role: "heading"
                text: dialog.title
            }

            AppText {
                Layout.fillWidth: true
                theme: dialog.theme
                role: "body"
                color: dialog.theme.inkSoft
                text: dialog.message
            }

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.theme.button
                theme: dialog.theme
                text: dialog.confirmText
                kind: dialog.danger ? "danger" : "primary"
                onClicked: {
                    dialog.close()
                    dialog.accepted()
                }
            }

            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.theme.button
                theme: dialog.theme
                text: dialog.cancelText
                kind: "secondary"
                onClicked: {
                    dialog.close()
                    dialog.cancelled()
                }
            }
        }
    }
}
