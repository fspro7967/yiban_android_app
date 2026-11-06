pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 设置里的开关行：整行 88dp 高，点击整行即可切换，滑块只是视觉反馈。
Item {
    id: row

    property Theme theme
    property string title: ""
    property string subtitle: ""
    property bool checked: false

    signal toggled(bool value)

    implicitHeight: theme.px(92)
    opacity: enabled ? 1.0 : 0.5

    property real _lastToggle: 0

    RowLayout {
        anchors.fill: parent
        spacing: row.theme.gap

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            AppText {
                Layout.fillWidth: true
                theme: row.theme
                role: "body"
                text: row.title
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }

            AppText {
                Layout.fillWidth: true
                visible: row.subtitle !== ""
                theme: row.theme
                role: "label"
                color: row.theme.inkSoft
                text: row.subtitle
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }

        Rectangle {
            id: track
            Layout.preferredWidth: row.theme.px(88)
            Layout.preferredHeight: row.theme.px(50)
            radius: height / 2
            color: row.checked ? row.theme.primary : "#B7AA96"

            Rectangle {
                id: knob
                width: parent.height - row.theme.px(8)
                height: width
                radius: width / 2
                y: row.theme.px(4)
                x: row.checked ? parent.width - width - row.theme.px(4) : row.theme.px(4)
                color: "#FFFFFF"

                Behavior on x {
                    NumberAnimation { duration: 140 }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            var t = Date.now()
            if (t - row._lastToggle < 300)
                return
            row._lastToggle = t
            row.toggled(!row.checked)
        }
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: title
    Accessible.checked: checked
    Accessible.onPressAction: row.toggled(!row.checked)
}
