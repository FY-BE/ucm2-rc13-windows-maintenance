import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "AppPalette.js" as Palette

ColumnLayout {
    id: root
    required property var report
    required property var progress
    spacing: 10

    function number(value, digits) {
        const parsed = Number(value)
        return value === undefined || value === null || !isFinite(parsed)
            ? "--" : parsed.toFixed(digits === undefined ? 3 : digits)
    }
    function reasonText(reason) {
        const names = {
            camera_quality: "相机识别或置信度未过门限",
            camera_values: "相机四杆读数无效",
            arm_unavailable: "200 ms 内无有效且未使用的 ARM 帧",
            ambiguous_pair: "最近 ARM 帧不唯一",
            time_gap: "两路时间差超限",
            geometry_or_delay: "几何量或时间差无效",
            image_evidence: "原图缺失或哈希不一致",
            outlier: "中位数/MAD 离群",
            "stage shorter than 3 seconds": "本档不足 3 秒",
            "fewer than 10 matched frames": "裁剪后配对不足 10 组",
            "fewer than 10 pairs after outlier rejection": "剔除离群点后不足 10 组",
            "sustained force or delay drift": "四杆力或时间差持续漂移",
            "no accepted stable stages": "没有通过筛查的稳定档",
            "shared Kmat and common b are not identifiable": "载荷变化不足，无法辨识共享 Kmat 与共同 b",
            "at least two distinct stable load stages are required": "至少需要两档不同的稳定载荷",
            "load coverage is too narrow across stages": "不同档位间的载荷变化不足",
            "load coverage is too narrow for a stable fit": "载荷覆盖太窄，拟合不稳定",
            "Windows clock discontinuity detected": "Windows 时钟发生跳变",
            "ARM configuration identity changed within the session": "采集期间 ARM 配置身份变化",
            "geometry changed within the calibration session": "采集期间几何量 G 变化"
        }
        return names[String(reason)] || String(reason || "")
    }
    function stageRepresentatives(stageNumber) {
        return (report.representatives || []).filter(function(row) { return row.stage === stageNumber })
    }
    function rejectionText(rejections) {
        const rows = []
        for (const key in (rejections || ({}))) {
            const count = Number(rejections[key])
            if (count > 0) rows.push(reasonText(key) + " " + count + " 组")
        }
        return rows.length ? rows.join("；") : "无"
    }
    function worstTrend(stage) {
        const checks = stage.trend_checks || []
        if (!checks.length) return "未完成趋势检查"
        let worst = checks[0]
        for (const check of checks) {
            if (check.drift_over_stage / Math.max(check.limit, 1e-12)
                > worst.drift_over_stage / Math.max(worst.limit, 1e-12)) worst = check
        }
        return "最大漂移：S" + worst.rod + (worst.channel === "force_kN" ? " 力" : " 时间差")
            + "，档内变化 " + number(worst.drift_over_stage, 4)
            + " / 阈值 " + number(worst.limit, 4)
            + (worst.channel === "force_kN" ? " kN" : " ns")
    }

    Label { text: report.preview ? "实时预拟合过程" : "拟合过程"
        font.pixelSize: 18; font.bold: true; color: Palette.text }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
        text: "采样核对 → 200 ms 内唯一配对 → 每档首尾各裁剪 0.5 秒 → MAD 离群与 Theil–Sen 漂移筛查 → 等档权重拟合 → 残差与留一验证。"
    }
    RowLayout {
        visible: progress.step !== undefined && progress.step !== ""
        Layout.fillWidth: true
        ProgressBar { from: 0; to: 100; value: Number(root.progress.percent || 0); Layout.preferredWidth: 260 }
        Label { text: Number(root.progress.percent || 0) + "% · " + String(root.progress.message || "")
            color: Palette.blue; Layout.fillWidth: true; wrapMode: Text.Wrap }
    }
    Label {
        visible: report.status === undefined
        text: "结束采集并拟合后，这里会保留每一步的输入数量、筛查依据和逐档计算值。"
        color: Palette.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    ColumnLayout {
        visible: report.status !== undefined
        Layout.fillWidth: true; spacing: 8
        Label {
            objectName: "fitInputSummary"
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
            text: "1  原始输入：相机 " + Number((report.input_counts || ({})).camera_frames || 0)
                + " 帧、ARM " + Number((report.input_counts || ({})).arm_frames || 0)
                + " 帧、划定 " + Number((report.input_counts || ({})).marked_stages || 0)
                + " 档。ROI、配置身份、帧身份和时钟先核对；"
                + (report.preview ? "预览只检查原图存在，结束后再核对哈希。" : "已核对原图哈希。")
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
            text: "2  配对与筛查：通过 " + Number(report.accepted_stages || 0)
                + " 档、用于拟合 " + Number(report.accepted_pairs || 0)
                + " 组。配对差中位数 " + root.number((report.pair_gap_ms || ({})).median, 1)
                + " ms，95% 分位 " + root.number((report.pair_gap_ms || ({})).p95, 1)
                + " ms。剔除：" + root.rejectionText(report.rejections)
        }
        Repeater {
            model: report.stages || []
            Rectangle {
                id: stageCard
                required property var modelData
                required property int index
                property bool expanded: false
                Layout.fillWidth: true
                implicitHeight: stageBody.implicitHeight + 20
                color: modelData.status === "accepted" ? "#F1F8F6" : "#FFF7F5"
                border.color: modelData.status === "accepted" ? "#C8E6DC" : "#F0D8D3"
                radius: 7
                ColumnLayout {
                    id: stageBody
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.margins: 10
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "第 " + (stageCard.index + 1) + " 档 · "
                                + (stageCard.modelData.status === "accepted" ? "通过" : "拒绝")
                            font.bold: true; color: stageCard.modelData.status === "accepted" ? "#168672" : Palette.red
                        }
                        Label {
                            text: root.number(stageCard.modelData.duration_ms / 1000, 1) + " 秒 · "
                                + "档内相机 " + Number(stageCard.modelData.camera_in_stage || 0)
                                + " · 裁剪过渡 " + Number(stageCard.modelData.transition_trimmed || 0)
                                + " · 配对 " + Number(stageCard.modelData.matched || 0)
                                + " · 保留 " + Number(stageCard.modelData.accepted || 0)
                            color: Palette.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap
                        }
                        AppButton { text: stageCard.expanded ? "收起明细" : "展开明细"
                            onClicked: stageCard.expanded = !stageCard.expanded }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                        text: stageCard.modelData.reason ? root.reasonText(stageCard.modelData.reason)
                            : "MAD 离群 " + Number(stageCard.modelData.outliers || 0)
                              + " 组；配对时间差中位数 " + root.number(stageCard.modelData.median_pair_gap_ms, 1)
                              + " ms；" + root.worstTrend(stageCard.modelData)
                    }
                    ColumnLayout {
                        visible: stageCard.expanded; Layout.fillWidth: true; spacing: 4
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                            text: "本档剔除：" + root.rejectionText(stageCard.modelData.rejections) }
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                            text: root.worstTrend(stageCard.modelData) }
                        Label {
                            visible: root.stageRepresentatives(stageCard.modelData.number).length > 0
                            text: "代表值：每杆对本档保留样本取中位数；拟合时每档每杆各占一个观测。"
                            color: Palette.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        Repeater {
                            model: root.stageRepresentatives(stageCard.modelData.number)
                            Label {
                                required property var modelData
                                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
                                text: "S" + (modelData.rod + 1)
                                    + "  力 " + root.number(modelData.force_kN, 3) + " kN"
                                    + "  ·  G " + root.number(modelData.g, 4)
                                    + "  ·  实测 ΔT " + root.number(modelData.dt_ns, 4) + " ns"
                                    + (modelData.predicted_dt_ns === undefined ? ""
                                       : "  ·  拟合 " + root.number(modelData.predicted_dt_ns, 4)
                                         + " ns  ·  残差 " + root.number(modelData.residual_ns, 4) + " ns")
                            }
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
            text: report.status === "candidate"
                ? "3  参数拟合：ΔTᵢ = 2 × Kmat × G × 10⁹ × Fᵢ(N) + b；"
                  + "共享 Kmat、共同 b 以等档权重做稳健回归，b 复制到 ARM 四槽。四杆独立 kᵢ、bᵢ 仅供诊断。迭代 "
                  + Number(report.irls_iterations || 0) + " 次，矩阵条件数 "
                  + root.number(report.condition_number, 2) + "。"
                : "3  参数拟合：" + root.reasonText(report.reason || "未进入拟合")
        }
        Label {
            visible: report.status === "candidate"
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.secondary
            text: "4  验证：逐杆残差、不确定度和载荷范围见下方结果。"
                + (report.independent_validation === "insufficient_stages"
                   ? "有效档数不足，独立留一验证未完成。"
                   : "留一验证结果：" + JSON.stringify(report.independent_validation))
        }
        Label {
            visible: report.status !== "candidate"
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Palette.red
            text: "本轮未产生可确认的候选：" + root.reasonText(report.reason)
        }
    }
}
