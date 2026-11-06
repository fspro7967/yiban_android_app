pragma ComponentBehavior: Bound

import QtQuick

// 统一文本组件：字号只从 Theme 取，保证三档字号全局一致。
Text {
    id: label

    property Theme theme
    // display / hero / title / heading / body / label / button
    property string role: "body"

    readonly property int roleSize: role === "display" ? theme.fontDisplay
        : role === "hero" ? theme.fontHero
        : role === "title" ? theme.fontTitle
        : role === "heading" ? theme.fontHeading
        : role === "label" ? theme.fontLabel
        : role === "button" ? theme.fontButton
        : theme.fontBody

    font.family: theme.fontFamily
    font.pixelSize: roleSize
    font.weight: (role === "hero" || role === "title" || role === "heading") ? Font.DemiBold : Font.Normal
    color: theme.ink
    wrapMode: Text.Wrap
    lineHeight: 0.9
    textFormat: Text.PlainText
}
