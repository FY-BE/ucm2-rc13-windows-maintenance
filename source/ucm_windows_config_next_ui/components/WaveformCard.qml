import QtQuick
import "AppPalette.js" as Palette

GlassPanel {
    id: root
    performanceSensitive: true
    property var series: []
    property int windowStart: 0
    property real sampleRateHz: 0
    property string statusText: "等待真实USB波形"
    property real cursorRatio: 0.5
    property bool cursorLocked: false
    readonly property int sampleCount: series && series.length === 4
                                       ? series[0].length : 0
    readonly property int cursorSample: sampleCount > 0
        ? Math.round(Math.max(0, Math.min(1, cursorRatio)) * (sampleCount - 1))
        : -1
    readonly property var colors: [Palette.rod1, Palette.rod2, Palette.rod3, Palette.rod4]
    readonly property var maxima: {
        const result = [1, 1, 1, 1]
        if (!root.series || root.series.length !== 4)
            return result
        for (let rod = 0; rod < 4; ++rod) {
            for (let sample = 0; sample < root.series[rod].length; ++sample)
                result[rod] = Math.max(result[rod], Math.abs(root.series[rod][sample]))
        }
        return result
    }

    function adcText(rod) {
        if (cursorSample < 0 || !series || rod >= series.length
                || cursorSample >= series[rod].length)
            return "--"
        return String(series[rod][cursorSample]) + " ADC"
    }

    function timeText() {
        if (cursorSample < 0 || sampleRateHz <= 0)
            return "--"
        return (cursorSample * 1000000.0 / sampleRateHz).toFixed(3) + " us"
    }

    onSeriesChanged: {
        plot.requestPaint()
        cursorOverlay.requestPaint()
    }
    onCursorRatioChanged: cursorOverlay.requestPaint()
    onCursorLockedChanged: cursorOverlay.requestPaint()

    Text {
        id: titleLabel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 20
        text: "原始采样波形 · 2048×4"
        color: Palette.text
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }
    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        text: root.sampleRateHz > 0
              ? (root.sampleRateHz / 1000000).toFixed(1) + " MSPS"
              : "latest-only"
        color: "#8B847E"
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 11
    }

    Canvas {
        id: plot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleLabel.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 74
        anchors.rightMargin: 20
        anchors.topMargin: 14
        anchors.bottomMargin: 18
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const laneHeight = height / 4
            ctx.strokeStyle = "#287E7770"
            ctx.lineWidth = 1
            for (let rod = 0; rod < 4; ++rod) {
                const top = rod * laneHeight
                ctx.beginPath(); ctx.moveTo(0, top); ctx.lineTo(width, top); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(0, top + laneHeight / 2)
                ctx.lineTo(width, top + laneHeight / 2); ctx.stroke()
            }
            for (let gx = 0; gx <= 8; ++gx) {
                const x = gx * width / 8
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
            if (root.sampleCount < 2)
                return

            for (let rod = 0; rod < 4; ++rod) {
                ctx.strokeStyle = root.colors[rod]
                ctx.lineWidth = 1.5
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                for (let sample = 0; sample < root.sampleCount; ++sample) {
                    const x = sample / (root.sampleCount - 1) * width
                    const center = rod * laneHeight + laneHeight / 2
                    const y = center - root.series[rod][sample]
                                      / root.maxima[rod] * laneHeight * 0.42
                    if (sample === 0) ctx.moveTo(x, y)
                    else ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }

        MouseArea {
            id: cursorArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            onPositionChanged: function(mouse) {
                if (!root.cursorLocked)
                    root.cursorRatio = Math.max(0, Math.min(1, mouse.x / width))
            }
            onClicked: function(mouse) {
                if (root.sampleCount === 0)
                    return
                root.cursorRatio = Math.max(0, Math.min(1, mouse.x / width))
                root.cursorLocked = !root.cursorLocked
            }
            onEntered: cursorOverlay.requestPaint()
            onExited: cursorOverlay.requestPaint()
        }
    }

    Canvas {
        id: cursorOverlay
        anchors.fill: plot
        visible: root.sampleCount > 1
                 && (cursorArea.containsMouse || root.cursorLocked)
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (root.cursorSample < 0 || root.sampleCount < 2)
                return
            const cursorX = root.cursorSample / (root.sampleCount - 1) * width
            const laneHeight = height / 4
            ctx.setLineDash([5, 4])
            ctx.strokeStyle = "#7F8C9B"
            ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(cursorX, 0); ctx.lineTo(cursorX, height); ctx.stroke()
            ctx.setLineDash([])
            for (let rod = 0; rod < 4; ++rod) {
                const center = rod * laneHeight + laneHeight / 2
                const y = center - root.series[rod][root.cursorSample]
                                  / root.maxima[rod] * laneHeight * 0.42
                ctx.fillStyle = root.colors[rod]
                ctx.strokeStyle = "#FFFFFF"
                ctx.lineWidth = 2
                ctx.beginPath(); ctx.arc(cursorX, y, 4.5, 0, Math.PI * 2)
                ctx.fill(); ctx.stroke()
            }
        }
    }

    Repeater {
        model: 4
        delegate: Text {
            x: 18
            y: plot.y + index * plot.height / 4 + plot.height / 8 - height / 2
            text: "杆" + (index + 1)
            color: root.colors[index]
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    Text {
        visible: root.sampleCount === 0
        anchors.centerIn: plot
        text: root.statusText
        color: "#857E78"
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 12
    }

    GlassPanel {
        visible: root.sampleCount > 0
                 && (cursorArea.containsMouse || root.cursorLocked)
        width: 202
        height: 132
        cornerRadius: 16
        glassColor: Palette.surface
        x: Math.min(Math.max(plot.x + root.cursorRatio * plot.width + 12,
                             plot.x), plot.x + plot.width - width)
        y: plot.y + 8
        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 5
            Text {
                text: (root.cursorLocked ? "已锁定" : "跟随")
                      + "  sample " + root.cursorSample
                color: Palette.text
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Text {
                text: "abs " + (root.windowStart + root.cursorSample)
                      + " · " + root.timeText()
                color: "#77716B"
                font.family: "Cascadia Mono"
                font.pixelSize: 10
            }
            Repeater {
                model: 4
                delegate: Text {
                    text: "杆" + (index + 1) + "   " + root.adcText(index)
                    color: root.colors[index]
                    font.family: "Cascadia Mono"
                    font.pixelSize: 10
                }
            }
        }
    }
}
