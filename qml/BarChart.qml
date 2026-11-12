pragma ComponentBehavior: Bound

import QtQuick

// 纯 Canvas 柱状图（按路线图 D16：不用 GPL 的 Qt Charts）。
// 7 天显示数值；30 天只显示起点/每 5 天/今天，避免拥挤。
Canvas {
    id: chart

    property Theme theme
    property var values: []

    onValuesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    Connections {
        target: chart.theme
        function onScaleLevelChanged() {
            chart.requestPaint()
        }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        if (!values || values.length === 0 || width <= 0 || height <= 0)
            return

        var n = values.length
        var padLeft = theme.px(4)
        var padRight = theme.px(4)
        var top = theme.px(30)
        var bottom = theme.px(40)
        var w = width - padLeft - padRight
        var h = height - top - bottom
        if (w <= 0 || h <= 0)
            return

        var gap = n > 14 ? theme.px(2) : theme.px(6)
        var barW = (w - gap * (n - 1)) / n
        if (barW < 1) {
            barW = 1
            gap = 0
        }
        // 刚开始使用时只有一两天数据：柱子不撑满整行，按 7 天的宽度从左排起。
        var maxBarW = (w - theme.px(6) * 6) / 7
        if (barW > maxBarW)
            barW = maxBarW

        // 50% 参考线
        ctx.strokeStyle = "#E8DCC8"
        ctx.lineWidth = 1
        ctx.beginPath()
        ctx.moveTo(padLeft, top + h * 0.5)
        ctx.lineTo(padLeft + w, top + h * 0.5)
        ctx.stroke()

        // 基线
        ctx.strokeStyle = theme.line
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.moveTo(padLeft, top + h)
        ctx.lineTo(padLeft + w, top + h)
        ctx.stroke()

        var valueSize = Math.max(12, Math.round(theme.fontChip * 0.8))
        var labelSize = Math.max(12, Math.round(theme.fontChip * 0.82))

        for (var i = 0; i < n; ++i) {
            var rate = Math.max(0, Math.min(100, values[i].rate))
            var barH = Math.max(2, h * rate / 100)
            var x = padLeft + i * (barW + gap)
            var y = top + h - barH
            var radius = Math.min(theme.px(5), barW / 2, barH / 2)

            ctx.fillStyle = i === n - 1 ? theme.primary : "#B7CDB2"
            ctx.beginPath()
            ctx.moveTo(x, y + barH)
            ctx.lineTo(x, y + radius)
            ctx.quadraticCurveTo(x, y, x + radius, y)
            ctx.lineTo(x + barW - radius, y)
            ctx.quadraticCurveTo(x + barW, y, x + barW, y + radius)
            ctx.lineTo(x + barW, y + barH)
            ctx.closePath()
            ctx.fill()

            if (n <= 7) {
                ctx.fillStyle = "#584B3C"
                ctx.font = "600 " + valueSize + "px '" + theme.fontFamily + "'"
                ctx.textAlign = "center"
                ctx.textBaseline = "bottom"
                ctx.fillText("" + Math.round(rate), x + barW / 2, y - theme.px(4))
            }

            if (values[i].label !== "") {
                ctx.fillStyle = i === n - 1 ? theme.primaryInk : theme.inkSoft
                ctx.font = (i === n - 1 ? "600 " : "400 ") + labelSize + "px '" + theme.fontFamily + "'"
                ctx.textAlign = "center"
                ctx.textBaseline = "top"
                ctx.fillText(values[i].label, x + barW / 2, top + h + theme.px(8))
            }
        }
    }
}
