pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 页面顶栏：左边是带字的“‹ 返回”，老人不用猜箭头的意思。
// 没有右侧操作时标题跟在返回键后面；有右侧操作（如“新建”）时标题另起一行，
// 大字号下也不会被两边的按钮挤成省略号。
Item {
    id: header

    property Theme theme
    property string title: ""
    property string subtitle: ""
    property bool showBack: true
    property string actionText: ""
    property string actionIcon: "plus"

    signal back()
    signal action()

    readonly property bool stacked: actionText !== ""

    implicitHeight: column.implicitHeight + theme.gapSmall * 2

    component TitleBlock: ColumnLayout {
        spacing: 0

        AppText {
            Layout.fillWidth: true
            theme: header.theme
            role: "title"
            font.pixelSize: header.theme.px(32)
            font.weight: Font.Bold
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

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: header.theme.gapSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: header.theme.gapSmall

            AppButton {
                objectName: "headerBack"
                visible: header.showBack
                Layout.preferredHeight: header.theme.touch
                Layout.preferredWidth: implicitWidth
                theme: header.theme
                text: "返回"
                iconName: "back"
                kind: "secondary"
                Accessible.name: "返回上一页"
                onClicked: header.back()
            }

            TitleBlock {
                visible: !header.stacked
                Layout.fillWidth: true
            }

            Item {
                visible: header.stacked
                Layout.fillWidth: true
            }

            AppButton {
                objectName: "headerAction"
                visible: header.stacked
                Layout.preferredHeight: header.theme.touch
                Layout.preferredWidth: implicitWidth
                theme: header.theme
                text: header.actionText
                iconName: header.actionIcon
                kind: "primary"
                onClicked: header.action()
            }
        }

        TitleBlock {
            visible: header.stacked
            Layout.fillWidth: true
        }
    }
}
