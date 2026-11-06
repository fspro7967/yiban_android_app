pragma ComponentBehavior: Bound

import QtQuick

// 全局视觉令牌：字号、间距、触控尺寸、高对比色板。
// 所有页面都只引用这里，避免逐处硬编码字号（需求 D12/D13）。
QtObject {
    id: theme

    // 0 = 标准（本身已按老年人放大），1 = 大，2 = 特大
    property int scaleLevel: 1

    readonly property real scale: scaleLevel === 0 ? 1.0 : (scaleLevel === 1 ? 1.15 : 1.3)

    function px(value) {
        return Math.round(value * scale)
    }

    readonly property string fontFamily: Qt.platform.os === "android" ? "sans-serif" : "Noto Sans CJK SC"

    // 基准字号（逻辑 px），再乘 scale
    readonly property int fontDisplay: px(68)
    readonly property int fontHero: px(46)
    readonly property int fontTitle: px(38)
    readonly property int fontHeading: px(30)
    readonly property int fontBody: px(26)
    readonly property int fontLabel: px(22)
    readonly property int fontButton: px(28)
    readonly property int fontChip: px(21)

    // 触控目标：所有可点元素 >= touch，主操作 >= buttonLarge
    readonly property int touch: px(64)
    readonly property int button: px(72)
    readonly property int buttonLarge: px(88)
    readonly property int reminderConfirm: px(112)

    readonly property int pageMargin: px(20)
    readonly property int gap: px(16)
    readonly property int gapSmall: px(10)
    readonly property int radius: px(20)
    readonly property int radiusSmall: px(14)
    readonly property int heroTime: px(60)

    // 高对比暖色系。正文 #211A12 on #FAF4E8 ≈ 15:1；次要文字 ≈ 7:1。
    readonly property color bg: "#FAF4E8"
    readonly property color surface: "#FFFFFF"
    readonly property color surfaceAlt: "#F2E9D8"
    readonly property color ink: "#211A12"
    readonly property color inkSoft: "#584B3C"
    readonly property color line: "#E0D2BC"

    readonly property color primary: "#175E2C"
    readonly property color primarySoft: "#E3F1E0"
    readonly property color primaryInk: "#0E3F1B"

    readonly property color amber: "#9A4A00"
    readonly property color amberSoft: "#FFEDC4"
    readonly property color amberInk: "#5A2D00"

    readonly property color danger: "#B3261E"
    readonly property color dangerSoft: "#FBE3E0"
    readonly property color dangerInk: "#6D0F0A"

    readonly property color dark: "#2A2219"
    readonly property color darkInk: "#FFF7EA"

    // 全屏提醒：深锈红底 + 琥珀强调，远看也醒目
    readonly property color reminderBgTop: "#682207"
    readonly property color reminderBgBottom: "#3B1002"
    readonly property color reminderInk: "#FFF4E2"
    readonly property color reminderAccent: "#FFC24B"
    readonly property color reminderButton: "#E6F6DE"
    readonly property color reminderButtonInk: "#103D18"
    readonly property color reminderGhost: "#FFD9A8"

    readonly property color sosBg: "#6B1210"
    readonly property color sosRed: "#C62828"

    readonly property color tileAmber: "#FFF0D2"
    readonly property color tileGreen: "#E6F2E3"
    readonly property color tileBlue: "#E7EEF9"
    readonly property color tileSand: "#F0E9DD"
}
