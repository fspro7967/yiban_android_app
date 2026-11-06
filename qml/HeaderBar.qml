pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 页面顶栏：返回键在左，标题居中区靠左，右侧可放“管理”等文字操作。
Item {
    id: header

    property Theme theme
    property string title: ""
    property string subtitle: ""
    property bool showBack: true
    property string actionText: ""

    signal back()
    signal action()

    implicitHeight: theme.px(94)

    RowLayout {
        anchors.fill: parent
        anchors.margins: header.theme.gapSmall
        spacing: header.theme.gapSmall

        AppButton {
            opacity: header.showBack ? 1 : 0
            enabled: header.showBack
            Layout.preferredWidth: header.showBack ? header.theme.px(64) : 0
            Layout.preferredHeight: header.theme.touch
            theme: header.theme
            text: "←"
            kind: "ghost"
            onClicked: header.back()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            AppText {
                Layout.fillWidth: true
                theme: header.theme
                role: "heading"
                text: header.title.length > 0 ? header.title : " "
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }

            AppText {
                Layout.fillWidth: true
                visible: header.subtitle !== ""
                theme: header.theme
                role: "label"
                color: header.theme.inkSoft
                text: header.subtitle
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }

        AppButton {
            opacity: header.actionText !== "" ? 1 : 0
            enabled: header.actionText !== ""
            Layout.preferredWidth: header.actionText !== "" ? header.theme.px(88) : 0
            Layout.preferredHeight: header.theme.touch
            theme: header.theme
            text: header.actionText
            kind: "ghost"
            onClicked: header.action()
        }
    }
}
