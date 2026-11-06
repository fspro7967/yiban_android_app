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

    signal clicked()

    property bool _pressed: false
    property real _lastActivated: 0

    implicitHeight: large ? theme.buttonLarge : theme.button
    implicitWidth: Math.max(theme.touch, content.implicitWidth + theme.px(36))
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
        width: parent.width - control.theme.px(24)
        spacing: control.theme.px(2)

        AppText {
            width: parent.width
            theme: control.theme
            role: "button"
            color: control.foreground
            font.weight: Font.DemiBold
            text: control.selected ? "✓ " + control.text : control.text
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
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
        if (t - _lastActivated < 400)
            return
        _lastActivated = t
        clicked()
    }
}
