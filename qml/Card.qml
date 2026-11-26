pragma ComponentBehavior: Bound

import QtQuick

// 白底卡片：浅描边 + 底部一条柔和投影。重点卡改 color（琥珀/浅绿）即可跳出来，
// 不再靠加粗描边区分层级。
Rectangle {
    id: card

    property Theme theme
    property bool raised: true

    radius: theme ? theme.radius : 20
    color: theme ? theme.surface : "#FFFFFF"
    border.width: 1
    border.color: theme ? theme.cardLine : "#EBE1D0"

    Rectangle {
        z: -1
        visible: card.raised
        anchors.fill: parent
        anchors.topMargin: 3
        anchors.bottomMargin: -3
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        radius: card.radius
        color: card.theme ? card.theme.shadow : "#18000000"
    }
}
