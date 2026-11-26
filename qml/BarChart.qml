pragma ComponentBehavior: Bound

import QtQuick

// 纯 Canvas 柱状图（按路线图 D16：不用 GPL 的 Qt Charts）。
// 7 天显示数值；30 天只显示起点/每 5 天/今天，避免拥挤。
// 默认画完成率（values[i].rate，0–100）；趋势图改 valueKey/maxValue/refValue 画杯数、分钟等。
Canvas {
    id: chart

    property Theme theme
    property var values: []
    property string valueKey: "rate"
    property real maxValue: 100
    property real refValue: maxValue / 2      // 参考虚线（完成率 50%、喝水目标 8 杯…）；<=0 不画
    property var formatValue: function (v) { return "" + Math.round(v) }
    // 柱顶要不要标数字（完成率图不标：每根都写 100/50 读起来累，看颜色和高度就够了）。
    property bool showValues: true
    // 两色模式：达到 maxValue 的柱子深绿（全做完），没达到的琥珀（没全做完）；否则今天深绿、其余浅绿。
    property bool twoTone: false
    // 今天那一根的横轴文字，空串就用原来的星期/日期。
    property string todayLabel: ""

    onShowValuesChanged: requestPaint()
    onTwoToneChanged: requestPaint()

    onMaxValueChanged: requestPaint()
    onRefValueChanged: requestPaint()

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
        var top = showValues ? theme.px(30) : theme.px(10)
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

        var maxV = maxValue > 0 ? maxValue : 1

        // 参考线
        if (refValue > 0 && refValue <= maxV) {
            var refY = top + h * (1 - refValue / maxV)
            ctx.strokeStyle = "#E8DCC8"
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(padLeft, refY)
            ctx.lineTo(padLeft + w, refY)
            ctx.stroke()
        }

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
            var raw = Number(values[i][valueKey]) || 0
            var v = Math.max(0, Math.min(maxV, raw))
            var barH = Math.max(2, h * v / maxV)
            var x = padLeft + i * (barW + gap)
            var y = top + h - barH
            var radius = Math.min(theme.px(5), barW / 2, barH / 2)

            ctx.fillStyle = twoTone ? (raw >= maxV ? theme.primary : theme.amberBar)
                : (i === n - 1 ? theme.primary : "#B7CDB2")
            ctx.beginPath()
            ctx.moveTo(x, y + barH)
            ctx.lineTo(x, y + radius)
            ctx.quadraticCurveTo(x, y, x + radius, y)
            ctx.lineTo(x + barW - radius, y)
            ctx.quadraticCurveTo(x + barW, y, x + barW, y + radius)
            ctx.lineTo(x + barW, y + barH)
            ctx.closePath()
            ctx.fill()

            if (n <= 7 && showValues) {
                ctx.fillStyle = "#584B3C"
                ctx.font = "600 " + valueSize + "px '" + theme.fontFamily + "'"
                ctx.textAlign = "center"
                ctx.textBaseline = "bottom"
                ctx.fillText(formatValue(raw), x + barW / 2, y - theme.px(4))
            }

            var label = (i === n - 1 && todayLabel !== "") ? todayLabel : values[i].label
            if (label !== "") {
                ctx.fillStyle = i === n - 1 ? theme.ink : theme.inkSoft
                ctx.font = (i === n - 1 ? "700 " : "400 ") + labelSize + "px '" + theme.fontFamily + "'"
                ctx.textAlign = "center"
                ctx.textBaseline = "top"
                ctx.fillText(label, x + barW / 2, top + h + theme.px(8))
            }
        }
    }
}
