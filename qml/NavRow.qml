pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 首页四个全宽大入口：整行可点，避免小图标误触。
Rectangle {
    id: row

    property Theme theme
    property string glyph: ""
    property string title: ""
    property string subtitle: ""
    property color tint: "#FFF0D2"
    property color badgeColor: "#5A2D00"

    signal clicked()

    implicitHeight: theme.px(112)
    radius: theme.radius
    color: tint
    border.width: 1
    border.color: Qt.rgba(0, 0, 0, 0.05)
    opacity: _pressed ? 0.9 : 1.0

    property bool _pressed: false

    RowLayout {
        anchors.fill: parent
        anchors.margins: row.theme.px(16)
        spacing: row.theme.px(14)

        Rectangle {
            Layout.preferredWidth: row.theme.px(58)
            Layout.preferredHeight: row.theme.px(58)
            radius: width / 2
            color: "#FFFFFF"
            border.width: 2
            border.color: Qt.rgba(0, 0, 0, 0.06)

            AppText {
                anchors.centerIn: parent
                theme: row.theme
                role: "heading"
                color: row.badgeColor
                text: row.glyph
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: row.theme.px(2)

            AppText {
                Layout.fillWidth: true
                theme: row.theme
                role: "heading"
                color: row.theme.ink
                text: row.title
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }

            AppText {
                Layout.fillWidth: true
                theme: row.theme
                role: "label"
                color: row.theme.inkSoft
                text: row.subtitle
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }

        AppText {
            theme: row.theme
            role: "title"
            color: row.badgeColor
            opacity: 0.65
            text: "›"
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: row._pressed = true
        onReleased: row._pressed = false
        onCanceled: row._pressed = false
        onClicked: row.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: title
}
