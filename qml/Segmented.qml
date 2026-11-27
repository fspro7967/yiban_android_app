pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// 分段切换：几个互斥选项放在一条浅底槽里，选中的一段填深绿。
// 用于“7 天 / 30 天”“每天 / 每周 / 每月 / 仅一次”这类单选，比一排独立按钮更像“切换”。
Rectangle {
    id: seg

    property Theme theme
    property var options: []
    property string current: ""

    signal picked(string value)

    implicitHeight: theme.touch + theme.px(8)
    radius: theme.radiusSmall + theme.px(4)
    color: theme.surfaceAlt

    property real _lastPick: 0

    RowLayout {
        anchors.fill: parent
        anchors.margins: seg.theme.px(4)
        spacing: seg.theme.px(4)

        Repeater {
            model: seg.options

            delegate: Rectangle {
                id: cell
                required property var modelData
                readonly property bool on: seg.current === modelData

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                radius: seg.theme.radiusSmall
                color: on ? seg.theme.primary : "transparent"

                AppText {
                    anchors.fill: parent
                    anchors.leftMargin: seg.theme.px(4)
                    anchors.rightMargin: seg.theme.px(4)
                    theme: seg.theme
                    role: "button"
                    font.pixelSize: Math.min(seg.theme.fontButton, seg.theme.px(25))
                    font.weight: Font.DemiBold
                    color: cell.on ? "#FFFFFF" : seg.theme.ink
                    text: cell.modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.NoWrap
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: seg.theme.px(16)
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        var t = Date.now()
                        if (Fmt.tooSoon(seg._lastPick, t, 300))
                            return
                        seg._lastPick = t
                        seg.picked(cell.modelData)
                    }
                }

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData
                Accessible.checked: on
            }
        }
    }
}
