pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 首页 2×2 大入口：整块可点，面积远大于最小触控目标，降低误触。
Rectangle {
    id: tile

    property Theme theme
    property string glyph: ""
    property string title: ""
    property string subtitle: ""
    property color tint: theme ? theme.tileSand : "#F0E9DD"
    property color badgeColor: theme ? theme.inkSoft : "#584B3C"

    signal clicked()

    implicitHeight: theme ? theme.px(88) : 88
    radius: theme ? theme.radius : 20
    color: theme ? theme.surface : "#FFFFFF"
    border.width: 2
    border.color: theme ? theme.line : "#E0D2BC"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: tile.theme ? tile.theme.px(10) : 10
        spacing: tile.theme ? tile.theme.px(2) : 2

        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                Layout.preferredWidth: tile.theme ? tile.theme.px(38) : 38
                Layout.preferredHeight: tile.theme ? tile.theme.px(38) : 38
                radius: width / 2
                color: tile.tint

                AppText {
                    anchors.centerIn: parent
                    theme: tile.theme
                    role: "body"
                    color: tile.badgeColor
                    text: tile.glyph
                }
            }

            Item { Layout.fillWidth: true }

            AppText {
                theme: tile.theme
                role: "body"
                color: tile.theme ? tile.theme.inkSoft : "#584B3C"
                text: "›"
                opacity: 0.55
            }
        }

        Item { Layout.fillHeight: true }

        AppText {
            Layout.fillWidth: true
            theme: tile.theme
            role: "body"
            text: tile.title
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
        }

    }

    MouseArea {
        anchors.fill: parent
        pressAndHoldInterval: 0

        property real lastClick: 0

        onPressed: tile.scale = 0.985
        onReleased: tile.scale = 1.0
        onCanceled: tile.scale = 1.0
        onClicked: {
            var t = Date.now()
            if (t - lastClick < 400)
                return
            lastClick = t
            tile.clicked()
        }

        Accessible.role: Accessible.Button
        Accessible.name: tile.title + "，" + tile.subtitle
    }

    Behavior on scale {
        NumberAnimation { duration: 90 }
    }
}
