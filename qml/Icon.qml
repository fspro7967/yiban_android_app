pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

// 线条图标：24×24 坐标系里的 SVG 路径，按 size 等比缩放，颜色跟随 color。
// 用 QtQuick.Shapes 现画，不打包图片，三档字号下都清晰，也能随主题改色。
Item {
    id: icon

    property string name: ""
    property color color: "#211A12"
    property real size: 24
    // 线宽按 24 坐标系写（和 SVG 源一致），实际像素随 size 缩放。
    property real strokeWidth: 2.2
    // 需要“实心”效果时给个填充色（如已喝的水杯），默认只画线。
    property color fill: "transparent"

    implicitWidth: size
    implicitHeight: size

    function circle(cx, cy, r) {
        return "M" + (cx - r) + " " + cy + " a" + r + " " + r + " 0 1 0 " + (2 * r) + " 0"
            + " a" + r + " " + r + " 0 1 0 " + (-2 * r) + " 0 "
    }

    readonly property var paths: ({
        "bell": "M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9 M10.3 21a1.94 1.94 0 0 0 3.4 0",
        "pencil": "M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z",
        "chart": "M3 3v18h18 M8 17v-5 M13 17V8 M18 17v-3",
        "sliders": "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6",
        "check": "M20 6 9 17l-5-5",
        "back": "M15 18l-6-6 6-6",
        "chevronRight": "M9 18l6-6-6-6",
        "chevronDown": "M6 9l6 6 6-6",
        "chevronUp": "M18 15l-6-6-6 6",
        "plus": "M12 5v14 M5 12h14",
        "minus": "M5 12h14",
        "close": "M18 6 6 18 M6 6l12 12",
        "speaker": "M11 5 6 9H2v6h4l5 4V5z M15.54 8.46a5 5 0 0 1 0 7.07 M19.07 4.93a10 10 0 0 1 0 14.14",
        "clock": circle(12, 12, 10) + "M12 6v6l4 2",
        "info": circle(12, 12, 10) + "M12 16v-4 M12 8h.01",
        "alert": "M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z M12 9v4 M12 17h.01",
        "pill": "M10.5 20.5l10-10a4.95 4.95 0 1 0-7-7l-10 10a4.95 4.95 0 1 0 7 7Z M8.5 8.5l7 7",
        "droplet": "M12 22a7 7 0 0 0 7-7c0-2-1-3.9-3-5.5s-3.5-4-4-6.5c-.5 2.5-2 4.9-4 6.5C6 11.1 5 13 5 15a7 7 0 0 0 7 7z",
        "activity": "M22 12h-4l-3 9L9 3l-3 9H2",
        "clipboard": "M9 2h6a1 1 0 0 1 1 1v2a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1z M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2 M12 11v6 M9 14h6",
        "phone": "M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72c.13.96.36 1.9.7 2.81a2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45c.91.34 1.85.57 2.81.7A2 2 0 0 1 22 16.92z",
        "more": circle(5, 12, 1) + circle(12, 12, 1) + circle(19, 12, 1),
        "undo": "M9 14 4 9l5-5 M4 9h10.5a5.5 5.5 0 0 1 0 11H11",
        "refresh": "M21 12a9 9 0 1 1-3-6.7L21 8 M21 3v5h-5",
        "moon": "M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z",
        "sun": circle(12, 12, 4) + "M12 2v2 M12 20v2 M4.93 4.93l1.41 1.41 M17.66 17.66l1.41 1.41 M2 12h2 M20 12h2 M4.93 19.07l1.41-1.41 M17.66 6.34l1.41-1.41",
        "cup": "M15.2 22H8.8a2 2 0 0 1-2-1.79L5 3h14l-1.81 17.21A2 2 0 0 1 15.2 22Z",
        "walk": circle(13, 4, 2) + "M7 22l3-7 M14 22l-1-5-3-3 1-5 M8 10l3-2 3 3 3 1",
        "home": "M3 10.5 12 3l9 7.5 M5 9v11h14V9",
        "cloudOff": "M2 2l20 20 M5.78 5.78A6 6 0 0 0 7 17h10 M20.4 15.4A4.5 4.5 0 0 0 17.5 9h-1.26A8 8 0 0 0 9.6 4.4",
        "pause": circle(12, 12, 10) + "M10 15V9 M14 15V9",
        "heart": "M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z"
    })

    readonly property string pathData: paths[name] !== undefined ? paths[name] : ""
    readonly property real k: size / 24

    Shape {
        anchors.fill: parent
        visible: icon.pathData !== ""
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            scale: Qt.size(icon.k, icon.k)
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth * icon.k
            fillColor: icon.fill
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg { path: icon.pathData }
        }
    }
}
