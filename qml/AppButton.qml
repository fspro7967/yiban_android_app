pragma ComponentBehavior: Bound

import QtQuick

// 大触控目标按钮。所有点击都有 400ms 去抖，避免手抖连点（R3-9）。
Rectangle {
    id: control

    property Theme theme
    property string text: ""
    property string subtitle: ""
    // primary / secondary / ghost / light / danger / outlineLight
    property string kind: "primary"
    property bool selected: false
    property bool large: false
    // 文字前的线条图标名（见 Icon.qml）；选中时固定显示对勾。
    property string iconName: ""
    readonly property string shownIcon: selected ? "check" : iconName

    signal clicked()

    property bool _pressed: false
    property real _lastActivated: 0

    implicitHeight: large ? theme.buttonLarge : theme.button
    // 自然宽度只看文字和图标本身，不读 content 的宽度（content 跟着按钮宽度走，读它会绑定循环）。
    implicitWidth: Math.max(theme.touch, mainText.implicitWidth + theme.px(30)
                            + (shownIcon !== "" ? Math.round(theme.fontButton * 1.05) + theme.px(8) : 0))
    radius: theme.radiusSmall
    color: control.selected ? theme.primary
        : kind === "primary" ? theme.primary
        : kind === "danger" ? theme.danger
        : kind === "secondary" ? theme.surface
        : kind === "light" ? "#FFFFFF"
        : "transparent"
    border.width: (kind === "secondary" || kind === "outlineLight") ? 2 : 0
    border.color: kind === "secondary" ? theme.line
        : kind === "outlineLight" ? theme.reminderGhost
        : "transparent"
    opacity: enabled ? 1.0 : 0.45
    scale: _pressed ? 0.985 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 90 }
    }

    readonly property color foreground: control.selected || kind === "primary" || kind === "danger" ? "#FFFFFF"
        : kind === "light" ? theme.dark
        : kind === "outlineLight" ? theme.reminderInk
        : theme.primary

    Column {
        id: content
        anchors.centerIn: parent
        width: parent.width - control.theme.px(16)
        spacing: control.theme.px(2)

        // 图标 + 文字整体居中；选中态用对勾图标表达，不再往文字前面拼“✓ ”。
        Item {
            width: parent.width
            height: mainText.implicitHeight

            Row {
                anchors.centerIn: parent
                spacing: control.theme.px(8)

                Icon {
                    id: leadIcon
                    anchors.verticalCenter: parent.verticalCenter
                    visible: control.shownIcon !== ""
                    name: control.shownIcon
                    size: Math.round(control.theme.fontButton * 1.05)
                    strokeWidth: 2.8
                    color: control.foreground
                }

                AppText {
                    id: mainText
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, content.width
                                    - (leadIcon.visible ? leadIcon.width + control.theme.px(8) : 0))
                    theme: control.theme
                    role: "button"
                    color: control.foreground
                    font.weight: Font.DemiBold
                    text: control.text
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.NoWrap
                    // 窄按钮里先把字缩小一点放下，实在放不下才省略。
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: control.theme.px(15)
                    elide: Text.ElideRight
                }
            }
        }

        AppText {
            width: parent.width
            visible: control.subtitle !== ""
            theme: control.theme
            role: "label"
            color: control.foreground
            opacity: 0.85
            text: control.subtitle
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.NoWrap
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: control.theme.px(13)
            elide: Text.ElideRight
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: control.enabled
        onPressed: control._pressed = true
        onReleased: control._pressed = false
        onCanceled: control._pressed = false
        onClicked: control.activate()
    }

    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.onPressAction: control.activate()

    function activate() {
        if (!enabled)
            return
        var t = Date.now()
        if (Fmt.tooSoon(_lastActivated, t, 400))
            return
        _lastActivated = t
        clicked()
    }
}
