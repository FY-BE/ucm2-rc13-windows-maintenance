import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

ColumnLayout {
    id: root
    required property var report
    property var hoverPoint: null
    property var hitPoints: []
    readonly property var colors: [Palette.rod1, Palette.rod2, Palette.rod3, Palette.rod4]
    spacing: 8

    function fmt(value, digits) {
        const number = Number(value)
        return value === undefined || value === null || !isFinite(number) ? "--" : number.toFixed(digits)
    }
    function modeledForce(sample, rod) {
        return modeledForceAt(Number((sample.ncc_delta_ns || [])[rod]), Number(sample.g), rod)
    }
    function modeledForceAt(delay, g, rod) {
        const kmat = Number(report.kmat_unified)
        const divisor = 2 * kmat * g * 1e12
        if (report.status !== "candidate" || !isFinite(divisor)
                || divisor <= 0 || !isFinite(delay)) return NaN
        const coupling = Number((report.coupling_bias_ns || [])[rod])
        return isFinite(coupling) ? correctedForceKn((delay - coupling) / divisor) : NaN
    }
    function correctedForceKn(baseForceKn) {
        const correction = report.force_correction || ({})
        const count = Number(correction.knot_count || 0)
        const inputs = correction.input_force_n || []
        const outputs = correction.output_force_n || []
        if (count < 2 || inputs.length < count || outputs.length < count)
            return baseForceKn
        const value = baseForceKn * 1000
        let segment = count - 2
        for (let index = 0; index < count - 1; ++index) {
            if (value <= Number(inputs[index + 1])) {
                segment = index
                break
            }
        }
        const x0 = Number(inputs[segment]), x1 = Number(inputs[segment + 1])
        const y0 = Number(outputs[segment]), y1 = Number(outputs[segment + 1])
        return x1 > x0 ? (y0 + (value - x0) * (y1 - y0) / (x1 - x0)) / 1000 : NaN
    }
    function diagnosticForceAt(delay, g, rod) {
        const diagnostic = (report.display_fits_by_rod || [])[rod] || ({})
        const divisor = 2 * Number(diagnostic.kmat) * g * 1e12
        return diagnostic.status === "diagnostic" && isFinite(divisor) && divisor > 0
            ? (delay - Number((report.coupling_bias_ns || [])[rod]) - Number(diagnostic.b_ns)) / divisor : NaN
    }
    function diagnosticSummary() {
        return (report.display_fits_by_rod || []).filter(function(row) {
            return row.status === "diagnostic"
        }).map(function(row) {
            return "S" + (Number(row.rod) + 1) + " k=" + Number(row.kmat).toExponential(3)
                + "、b=" + fmt(row.b_ns, 2) + " ns"
        }).join("；")
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: report.preview ? "实时预拟合 · DET_T–力图" : "DET_T–力拟合图"
            font.pixelSize: 18; font.bold: true; color: Palette.text }
        Item { Layout.fillWidth: true }
        Label {
            text: report.preview ? "采集中约每 2 秒更新 · 预览结果不可下发"
                : report.status === "candidate" ? "最终拟合" : "等待稳定载荷数据"
            color: report.preview ? "#B47721" : Palette.secondary
        }
    }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 360
        color: "#FFFFFF"; radius: 8; border.color: Palette.border
        Canvas {
            id: canvas
            anchors.fill: parent; anchors.margins: 8
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Component.onCompleted: requestPaint()
            Connections { target: root; function onReportChanged() { canvas.requestPaint() } }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width, h = height
                const left = 70, right = 24, top = 30, bottom = 44
                const plotW = Math.max(1, w - left - right)
                const plotH = Math.max(1, h - top - bottom)
                const samples = root.report.plot_samples || []
                root.hitPoints = []
                ctx.fillStyle = "#FFFFFF"; ctx.fillRect(0, 0, w, h)
                if (!samples.length) {
                    ctx.strokeStyle = "#8392A4"; ctx.lineWidth = 1.5
                    ctx.beginPath(); ctx.moveTo(left, top); ctx.lineTo(left, top + plotH)
                    ctx.lineTo(left + plotW, top + plotH); ctx.stroke()
                    ctx.font = "12px sans-serif"; ctx.fillStyle = Palette.secondary
                    ctx.textAlign = "left"; ctx.fillText("力 (kN)", 9, 17)
                    ctx.textAlign = "right"; ctx.fillText("DET_T (ns)", w - 3, h - 3)
                    ctx.font = "14px sans-serif"; ctx.fillStyle = Palette.secondary; ctx.textAlign = "center"
                    ctx.fillText("取得相机与 ARM 配对后显示 DET_T–力散点", w / 2, h / 2)
                    return
                }
                const delays = []
                const forces = []
                for (const sample of samples) for (let rod = 0; rod < 4; ++rod) {
                    const delay = Number((sample.ncc_delta_ns || [])[rod])
                    const cameraForce = Number((sample.force_kN || [])[rod])
                    const fittedForce = root.modeledForce(sample, rod)
                    if (isFinite(delay)) delays.push(delay)
                    if (isFinite(cameraForce)) forces.push(cameraForce)
                    if (isFinite(fittedForce)) forces.push(fittedForce)
                }
                if (!delays.length || !forces.length) return
                const minDelay = Math.min.apply(null, delays)
                const maxDelay = Math.max.apply(null, delays)
                const xSpan = Math.max(1, maxDelay - minDelay)
                const xMin = minDelay - xSpan * 0.12
                const xMax = maxDelay + xSpan * 0.12
                const minForce = Math.min.apply(null, forces)
                const maxForce = Math.max.apply(null, forces)
                const ySpan = Math.max(1, maxForce - minForce)
                const yMin = minForce - ySpan * 0.12
                const yMax = maxForce + ySpan * 0.12
                const mapX = function(delay) { return left + (delay - xMin) / (xMax - xMin) * plotW }
                const mapY = function(force) { return top + (yMax - force) / (yMax - yMin) * plotH }

                ctx.strokeStyle = "#E5ECF3"; ctx.lineWidth = 1
                ctx.fillStyle = "#63748A"; ctx.font = "11px sans-serif"
                for (let tick = 0; tick <= 5; ++tick) {
                    const x = left + tick * plotW / 5
                    const y = top + tick * plotH / 5
                    ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + plotH); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(left + plotW, y); ctx.stroke()
                    ctx.textAlign = "center"; ctx.fillText(root.fmt(xMin + tick * (xMax - xMin) / 5, 1), x, h - 23)
                    ctx.textAlign = "right"; ctx.fillText(root.fmt(yMax - tick * (yMax - yMin) / 5, 2), left - 8, y + 4)
                }
                ctx.strokeStyle = "#8392A4"; ctx.lineWidth = 1.5
                ctx.beginPath(); ctx.moveTo(left, top); ctx.lineTo(left, top + plotH)
                ctx.lineTo(left + plotW, top + plotH); ctx.stroke()
                ctx.font = "12px sans-serif"; ctx.fillStyle = Palette.secondary
                ctx.textAlign = "left"; ctx.fillText("力 (kN)", 9, 17)
                ctx.textAlign = "right"; ctx.fillText("DET_T (ns)", w - 3, h - 3)

                for (let rod = 0; rod < 4; ++rod) {
                    const color = root.colors[rod]
                    const rodDelays = samples.map(function(sample) {
                        return Number((sample.ncc_delta_ns || [])[rod])
                    }).filter(function(value) { return isFinite(value) })
                    if (rodDelays.length > 1) {
                        const first = Math.min.apply(null, rodDelays)
                        const last = Math.max.apply(null, rodDelays)
                        const g = Number(samples[0].g)
                        const firstForce = root.diagnosticForceAt(first, g, rod)
                        const lastForce = root.diagnosticForceAt(last, g, rod)
                        if (isFinite(firstForce) && isFinite(lastForce)) {
                            ctx.strokeStyle = color; ctx.globalAlpha = 0.72; ctx.lineWidth = 2.5
                            ctx.beginPath(); ctx.moveTo(mapX(first), mapY(firstForce))
                            ctx.lineTo(mapX(last), mapY(lastForce)); ctx.stroke()
                        }
                    }
                    ctx.strokeStyle = color; ctx.fillStyle = color
                    ctx.globalAlpha = 0.84; ctx.lineWidth = 1.5
                    for (const sample of samples) {
                        const delay = Number((sample.ncc_delta_ns || [])[rod])
                        const force = Number((sample.force_kN || [])[rod])
                        if (!isFinite(delay) || !isFinite(force)) continue
                        const x = mapX(delay), y = mapY(force)
                        if (sample.accepted) {
                            ctx.beginPath(); ctx.arc(x, y, 3.5, 0, Math.PI * 2); ctx.fill()
                        } else {
                            ctx.lineWidth = 2
                            ctx.beginPath(); ctx.moveTo(x - 5, y - 5); ctx.lineTo(x + 5, y + 5)
                            ctx.moveTo(x - 5, y + 5); ctx.lineTo(x + 5, y - 5); ctx.stroke()
                        }
                        root.hitPoints.push({x:x, y:y, rod:rod, kind:"sample", stage:sample.stage,
                                             delay:delay, force:force, g:Number(sample.g),
                                             accepted:sample.accepted,
                                             rejectionReason:String(sample.rejection_reason || ""),
                                             gapMs:Number(sample.pair_gap_ms)})
                    }
                    for (const representative of (root.report.representatives || [])) {
                        if (Number(representative.rod) !== rod) continue
                        const delay = Number(representative.raw_dt_ns)
                        const force = Number(representative.force_kN)
                        if (!isFinite(delay) || !isFinite(force)) continue
                        const x = mapX(delay), y = mapY(force)
                        ctx.globalAlpha = 1; ctx.lineWidth = 2
                        ctx.beginPath(); ctx.moveTo(x, y - 6); ctx.lineTo(x + 6, y)
                        ctx.lineTo(x, y + 6); ctx.lineTo(x - 6, y); ctx.closePath()
                        ctx.fill(); ctx.stroke()
                        root.hitPoints.push({x:x, y:y, rod:rod, kind:"representative",
                                             stage:representative.stage, delay:delay,
                                             force:force, g:Number(representative.g)})
                    }
                    ctx.globalAlpha = 1
                }
                if (root.report.status === "candidate"
                    && root.report.fit_constraint === "shared_kmat_shared_b") {
                    const g = Number(samples[0].g)
                    const biases = root.report.coupling_bias_ns || []
                    const separate = biases.some(function(value) { return Number(value) !== Number(biases[0]) })
                    for (let curveRod = 0; curveRod < (separate ? 4 : 1); ++curveRod) {
                        const firstForce = root.modeledForceAt(minDelay, g, curveRod)
                        if (!isFinite(firstForce)) continue
                        ctx.strokeStyle = separate ? root.colors[curveRod] : "#1B293D"
                        ctx.globalAlpha = 0.95; ctx.lineWidth = 4
                        ctx.beginPath(); ctx.moveTo(mapX(minDelay), mapY(firstForce))
                        for (let step = 1; step <= 64; ++step) {
                            const delay = minDelay + (maxDelay - minDelay) * step / 64
                            const force = root.modeledForceAt(delay, g, curveRod)
                            if (isFinite(force)) ctx.lineTo(mapX(delay), mapY(force))
                        }
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1
                }
            }
            MouseArea {
                anchors.fill: parent; hoverEnabled: true
                onPositionChanged: function(mouse) {
                    let found = null, best = 100
                    for (const point of root.hitPoints) {
                        const dx = point.x - mouse.x, dy = point.y - mouse.y
                        const distance = dx * dx + dy * dy
                        if (distance < best) { found = point; best = distance }
                    }
                    root.hoverPoint = found
                }
                onExited: root.hoverPoint = null
            }
            Rectangle {
                visible: root.hoverPoint !== null
                x: root.hoverPoint ? Math.min(canvas.width - width - 4, root.hoverPoint.x + 10) : 0
                y: root.hoverPoint ? Math.max(4, root.hoverPoint.y - height - 8) : 0
                width: tip.implicitWidth + 20; height: tip.implicitHeight + 12
                color: "#20324B"; radius: 5; z: 10
                Label {
                    id: tip; anchors.centerIn: parent; color: "white"; font.pixelSize: 11
                    text: root.hoverPoint
                        ? "第 " + root.hoverPoint.stage + " 档 S" + (root.hoverPoint.rod + 1)
                          + (root.hoverPoint.kind === "representative" ? " 代表值"
                             : root.hoverPoint.accepted ? " 配对点" : " 已剔除：" + root.hoverPoint.rejectionReason)
                          + " · DET_T " + root.fmt(root.hoverPoint.delay, 2) + " ns"
                          + " · 相机 " + root.fmt(root.hoverPoint.force, 3) + " kN"
                          + (isFinite(root.hoverPoint.gapMs)
                             ? " · 配对差 " + root.fmt(root.hoverPoint.gapMs, 0) + " ms" : "")
                          + (root.report.fit_constraint === "shared_kmat_shared_b"
                             ? " · 统一预测 " + root.fmt(root.modeledForceAt(
                                   root.hoverPoint.delay, root.hoverPoint.g, root.hoverPoint.rod), 3) + " kN" : "")
                        : ""
                }
            }
        }
    }
    RowLayout {
        spacing: 14
        Repeater {
            model: ["S1", "S2", "S3", "S4"]
            RowLayout {
                required property string modelData
                required property int index
                spacing: 5
                Rectangle { width: 12; height: 12; radius: 6; color: root.colors[index] }
                Label { text: modelData; color: Palette.secondary }
            }
        }
        Label { text: "圆点：保留配对；×：已剔除的可绘制点；菱形：档位代表值；四色细实线：各杆独立诊断拟合；深色粗线：共享 Kmat 与零锚定分段校正的运行预测"
            color: Palette.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
        text: report.status === "candidate"
            ? "下发候选：共享 Kmat = " + root.fmt(report.kmat_unified, 12)
              + "；共同 b = " + root.fmt(report.b_shared_ns, 3) + " ns（保留为拟合证据，不在相对 Δt 上再次扣除）"
              + "；分段校正点 " + Number((report.force_correction || ({})).knot_count || 0)
              + "；已通过 " + Number(report.accepted_stages || 0) + " 档。四色独立拟合仅供诊断。"
            : report.preview ? "当前尚不足以拟合；先显示已配对的 DET_T–相机力散点。" + String(report.reason || "")
            : "每档配对后更新散点；载荷覆盖足够时显示独立诊断细线与统一候选预测粗实线。"
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
        visible: root.diagnosticSummary().length > 0
        text: "独立诊断参数（不下发）：" + root.diagnosticSummary()
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
        visible: (report.screening_rules || ({})).pair_limit_ms !== undefined
        text: "时间戳配对窗口：前后各 " + Number((report.screening_rules || ({})).pair_limit_ms || 0)
            + " ms；实际差值中位数 " + root.fmt((report.pair_gap_ms || ({})).median, 1)
            + " ms，95% 分位 " + root.fmt((report.pair_gap_ms || ({})).p95, 1)
            + " ms。窗口上限不代表同步精度。"
    }
}
