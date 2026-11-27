pragma ComponentBehavior: Bound

import QtQuick

// 首页入口块：上面一个有底色的线条图标，下面两个字的名字，整块可点。
// 四块排成一行，省下的高度留给“现在该做”的主卡。
Card {
    id: tile

    property string iconName: ""
    property string title: ""
    property string subtitle: ""
    property color tint: theme ? theme.tileSand : "#F0E9DD"
    property color badgeColor: theme ? theme.inkSoft : "#584B3C"

    signal clicked()

    implicitHeight: theme ? theme.px(92) : 92
    radius: theme ? theme.radius - theme.px(2) : 18

    Column {
        anchors.centerIn: parent
        width: parent.width - (tile.theme ? tile.theme.px(8) : 8)
        spacing: tile.theme ? tile.theme.px(6) : 6

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: tile.theme ? tile.theme.px(44) : 44
            height: width
            radius: tile.theme ? tile.theme.px(14) : 14
            color: tile.tint

            Icon {
                anchors.centerIn: parent
                name: tile.iconName
                size: Math.round(parent.width * 0.58)
                color: tile.badgeColor
            }
        }

        AppText {
            width: parent.width
            theme: tile.theme
            role: "label"
            font.weight: Font.Bold
            text: tile.title
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.NoWrap
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 14
        }
    }

    MouseArea {
        anchors.fill: parent
        pressAndHoldInterval: 0

        property real lastClick: 0

        onPressed: tile.scale = 0.97
        onReleased: tile.scale = 1.0
        onCanceled: tile.scale = 1.0
        onClicked: {
            var t = Date.now()
            if (Fmt.tooSoon(lastClick, t, 400))
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
