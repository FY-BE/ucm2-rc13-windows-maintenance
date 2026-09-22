#include "reason_dictionary.h"

#include <array>

namespace ucm {
namespace {

struct ReasonLabel {
    const char *name;
    const char *chinese;
};

template <std::size_t Count>
QString labelFor(quint32 code, const std::array<ReasonLabel, Count> &labels,
                 const QString &unknown)
{
    if (code >= Count) return QStringLiteral("%1（code %2）").arg(unknown).arg(code);
    const ReasonLabel &label = labels[code];
    return QStringLiteral("%1 · %2")
        .arg(QString::fromLatin1(label.name),
             QString::fromUtf8(label.chinese));
}

const std::array<ReasonLabel, 8> kForceRodReasons {{
    {"OK", "正常"},
    {"MEASUREMENT_UNAVAILABLE", "测量不可用"},
    {"CALIBRATION_INVALID", "标定无效"},
    {"GEOMETRY_INVALID", "几何参数无效"},
    {"K0_INVALID", "K0换算系数无效"},
    {"NONFINITE", "计算出现非有限数"},
    {"NEGATIVE", "触发负力判定"},
    {"CONTEXT_CONFLICT", "运行上下文冲突"}
}};

const std::array<ReasonLabel, 30> kGateReasons {{
    {"ALGORITHM_BOOTING", "算法正在启动"},
    {"CONTEXT_LOADING", "正在加载运行上下文"},
    {"ACQUISITION_CONFIGURING", "正在配置采集链"},
    {"HEALTH_DEGRADED", "运行健康降级"},
    {"HEALTH_FAULT", "运行健康故障"},
    {"RUNTIME_MODE_NOT_PRODUCTION", "当前不是生产运行模式"},
    {"TOP_COMMAND_STOPPED", "顶层命令处于停止"},
    {"MODE_SWITCH_REQUIRES_STOPPED", "切换模式前必须停止"},
    {"WAITING_FOR_OPEN_COMMAND", "等待开模命令"},
    {"OPEN_STATE_NOT_READY", "开模状态尚未就绪"},
    {"PROCESS_STATE_MISSING", "过程状态缺失"},
    {"ACTIVE_CONTEXT_MISSING", "活动上下文缺失"},
    {"ACTIVE_CONTEXT_PENDING", "活动上下文仍在建立"},
    {"ACTIVE_CONTEXT_INVALID", "活动上下文无效"},
    {"CALIBRATION_INVALID", "标定无效"},
    {"GEOMETRY_OUT_OF_RANGE", "几何参数越界"},
    {"K0_INVALID", "K0换算系数无效"},
    {"DT_UNAVAILABLE", "时间差不可用"},
    {"DT_SELECTION_INCONSISTENT", "时间差选择不一致"},
    {"PARTIAL_RODS", "只有部分拉杆有效"},
    {"NEGATIVE_FORCE_TRIGGERED", "触发负力判定"},
    {"BELOW_MIN_LOAD_TRIGGERED", "总载荷低于有效门限"},
    {"ACQUISITION_PENDING", "采集调整等待执行"},
    {"ACQUISITION_APPLYING", "采集调整正在应用"},
    {"ACQUISITION_SETTLING", "采集调整后正在稳定"},
    {"ACQUISITION_REJECTED", "采集调整被拒绝"},
    {"ACQUISITION_FAILED", "采集调整失败"},
    {"ACQUISITION_TIMEOUT", "采集调整超时"},
    {"ACQUISITION_MISMATCH", "采集回执与请求不一致"},
    {"SUMMARY_FINGERPRINT_MISMATCH", "摘要指纹与上下文不一致"}
}};

const std::array<ReasonLabel, 141> kDiagnosticReasons {{
    {"ALGORITHM_BOOTING", "算法正在启动"},
    {"CONTEXT_LOADING", "正在加载运行上下文"},
    {"ACQUISITION_CONFIGURING", "正在配置采集链"},
    {"POLICY_REF_MISSING", "策略引用缺失"},
    {"POLICY_REF_UNRESOLVABLE", "策略引用无法解析"},
    {"POLICY_VALUE_INVALID", "策略值无效"},
    {"HEALTH_DEGRADED", "运行健康降级"},
    {"HEALTH_FAULT", "运行健康故障"},
    {"RUNTIME_MODE_NOT_PRODUCTION", "当前不是生产运行模式"},
    {"TOP_COMMAND_STOPPED", "顶层命令处于停止"},
    {"MODE_SWITCH_REQUIRES_STOPPED", "切换模式前必须停止"},
    {"WAITING_FOR_OPEN_COMMAND", "等待开模命令"},
    {"OPEN_COMMAND_RECEIVED", "已收到开模命令"},
    {"OPEN_STATE_NOT_READY", "开模状态尚未就绪"},
    {"OPEN_STABLE_NOT_CONFIRMED", "开模稳定尚未确认"},
    {"PROCESS_STATE_MISSING", "过程状态缺失"},
    {"PROCESS_STATE_UNKNOWN_OR_UNSUPPORTED", "过程状态未知或不支持"},
    {"PROCESS_STATE_MAPPING_UNAVAILABLE", "过程状态映射不可用"},
    {"PROCESS_STATE_ILLEGAL_TRANSITION", "过程状态发生非法跳转"},
    {"PROCESS_STATE_TIMEOUT_OR_TIMESTAMP_INVALID", "过程状态超时或时间戳无效"},
    {"PROCESS_STATE_STALE_OR_UNEXPECTED_STATIC", "过程状态陈旧或异常静止"},
    {"ACTIVE_CONTEXT_MISSING", "活动上下文缺失"},
    {"ACTIVE_CONTEXT_PENDING", "活动上下文仍在建立"},
    {"ACTIVE_CONTEXT_INVALID", "活动上下文无效"},
    {"SUMMARY_LAYOUT_MISMATCH", "摘要布局不匹配"},
    {"PHI_DYNAMIC_SOURCE_FORBIDDEN", "禁止使用动态Phi来源"},
    {"PHI_OVERRIDE_FORBIDDEN", "禁止覆盖Phi"},
    {"CALIBRATION_MISSING", "标定缺失"},
    {"CALIBRATION_INVALID", "标定无效"},
    {"CALIBRATION_INCOMPATIBLE", "标定与当前上下文不兼容"},
    {"GEOMETRY_MISSING", "几何参数缺失"},
    {"GEOMETRY_NONFINITE", "几何参数出现非有限数"},
    {"GEOMETRY_OUT_OF_RANGE", "几何参数越界"},
    {"PHI_MISSING", "Phi参数缺失"},
    {"PHI_NONFINITE", "Phi参数出现非有限数"},
    {"PHI_OUT_OF_RANGE", "Phi参数越界"},
    {"K0_INVALID", "K0换算系数无效"},
    {"KMAT_INVALID", "材料系数Kmat无效"},
    {"SUMMARY_MISSING", "测量摘要缺失"},
    {"SUMMARY_CRC_INVALID", "测量摘要CRC无效"},
    {"SUMMARY_LAYOUT_UNSUPPORTED", "测量摘要布局不支持"},
    {"SUMMARY_SCHEMA_VERSION_MISMATCH", "测量摘要schema版本不匹配"},
    {"SUMMARY_PAYLOAD_INCOMPLETE", "测量摘要载荷不完整"},
    {"SUMMARY_SLOT_TOO_SHORT", "摘要槽长度不足"},
    {"SUMMARY_MAGIC_MISMATCH", "摘要magic不匹配"},
    {"SUMMARY_LAYOUT_VERSION_UNSUPPORTED", "摘要布局版本不支持"},
    {"SUMMARY_HEADER_SIZE_MISMATCH", "摘要头长度不匹配"},
    {"SUMMARY_SLOT_SIZE_MISMATCH", "摘要槽长度不匹配"},
    {"SUMMARY_CONTROL_SECTION_SIZE_MISMATCH", "摘要控制区长度不匹配"},
    {"SUMMARY_CHANNEL_COUNT_MISMATCH", "摘要通道数量不匹配"},
    {"SUMMARY_CHANNEL_STRIDE_MISMATCH", "摘要通道步长不匹配"},
    {"SUMMARY_CRC_MISMATCH", "摘要CRC不匹配"},
    {"SUMMARY_ACQUISITION_STATUS_RESERVED_BIT_SET", "采集状态包含保留位"},
    {"SUMMARY_CHANNEL_ID_INVALID", "摘要通道ID无效"},
    {"SUMMARY_V1_NOT_ALLOWED_IN_PRODUCTION", "生产模式禁止V1摘要"},
    {"T0_UNAVAILABLE", "T0不可用"},
    {"T0_NONFINITE_OR_OUT_OF_RANGE", "T0非有限或越界"},
    {"T0_UNSTABLE_OR_JUMP", "T0不稳定或跳变"},
    {"THRESHOLD_CROSSING_INVALID", "阈值交点无效"},
    {"NCC_PEAK_UNAVAILABLE", "NCC峰值不可用"},
    {"NCC_PEAK_AMBIGUOUS", "NCC峰值存在歧义"},
    {"NCC_CONFIDENCE_TOO_LOW", "NCC置信度过低"},
    {"PEAKCORE_QUALITY_INVALID", "峰值核心质量无效"},
    {"WEAK_SIGNAL", "信号过弱"},
    {"SATURATION", "信号饱和"},
    {"LOW_SNR", "信噪比过低"},
    {"LOW_XCOR_CONFIDENCE", "互相关置信度过低"},
    {"UNSTABLE_T0_OR_DT", "T0或时间差不稳定"},
    {"PEAK_JUMP", "相关峰发生跳变"},
    {"PRODUCER_QUALITY_FLAGS_BLOCKING", "采集生产者质量标志阻断"},
    {"DT_UNAVAILABLE", "时间差不可用"},
    {"DT_PRIMARY_BACKUP_BOTH_INVALID", "主备时间差均无效"},
    {"DT_SELECTION_INCONSISTENT", "时间差选择不一致"},
    {"DT_OUTLIER_REJECTED", "时间差离群值被拒绝"},
    {"DT_TIE_BREAK_UNRESOLVED", "时间差平局无法裁决"},
    {"PARTIAL_RODS", "只有部分拉杆有效"},
    {"NEGATIVE_FORCE_TRIGGERED", "触发负力判定"},
    {"BELOW_MIN_LOAD_TRIGGERED", "总载荷低于有效门限"},
    {"ACQUISITION_PENDING", "采集调整等待执行"},
    {"ACQUISITION_APPLYING", "采集调整正在应用"},
    {"ACQUISITION_SETTLING", "采集调整后正在稳定"},
    {"ACQUISITION_REJECTED", "采集调整被拒绝"},
    {"ACQUISITION_FAILED", "采集调整失败"},
    {"ACQUISITION_TIMEOUT", "采集调整超时"},
    {"ACQUISITION_MISMATCH", "采集回执与请求不一致"},
    {"AGC_POLICY_BLOCKED", "AGC策略阻断"},
    {"AGC_LIMIT_REACHED", "AGC已到调节边界"},
    {"AGC_COOLDOWN_ACTIVE", "AGC处于冷却期"},
    {"AGC_FROZEN", "AGC已冻结"},
    {"AGC_FEEDBACK_TIMEOUT", "AGC反馈超时"},
    {"CADENCE_RESTORE_TIMEOUT", "采集节拍恢复超时"},
    {"OPEN_STABLE_THROTTLE", "开模稳定节拍受限"},
    {"WAKE_ON_CLOSING_OR_LOADING", "合模或加载触发唤醒"},
    {"POLICY_DISABLED_RESTORE", "策略禁用后恢复节拍"},
    {"CONFIG_CHANGED_RESTORE", "配置变化后恢复节拍"},
    {"EXTERNAL_COMMAND_REQUIRED", "需要外部命令"},
    {"HELD_PREVIOUS_ACTIVE", "正在保持上一显示值"},
    {"HOLD_TIMEOUT", "保持值超时"},
    {"FILTER_RESETTING", "滤波器正在复位"},
    {"FILTER_BYPASSED_INVALID_INPUT", "输入无效，滤波已旁路"},
    {"SCREEN_ZERO_UPDATE_BLOCKED", "屏幕零载参考刷新被阻断"},
    {"SCREEN_ZERO_UNSTABLE_STATE", "状态不稳定，不能更新屏幕零点"},
    {"SCREEN_ZERO_RESIDUAL_TOO_LARGE", "屏幕零载参考残差过大"},
    {"SCREEN_ZERO_POLICY_NOT_APPLICABLE", "当前不允许刷新零载参考"},
    {"MANUAL_TRIAL_REQUEST_REJECTED", "人工试验请求被拒绝"},
    {"NOT_IN_MANUAL_TRIAL_MODE", "当前不在人工试验模式"},
    {"INVALID_REQUEST_SEQ", "请求序号无效"},
    {"UNSUPPORTED_MANUAL_ACTION", "不支持的人工动作"},
    {"INVALID_TARGET_KIND", "目标类型无效"},
    {"TARGET_REF_INVALID", "目标引用无效"},
    {"PRODUCTION_ASSET_PROMOTION_FORBIDDEN", "禁止提升生产资产"},
    {"SAFETY_GATE_BLOCKED", "安全门阻断"},
    {"PROCESS_STATE_UNTRUSTED", "过程状态不可信"},
    {"ACQUISITION_BUSY", "采集链忙"},
    {"HARDWARE_ROUTE_NOT_AVAILABLE", "硬件路径不可用"},
    {"FEEDBACK_TIMEOUT", "反馈超时"},
    {"FEEDBACK_SEQ_MISMATCH", "反馈序号不匹配"},
    {"TRACE_OPEN_FAILED", "跟踪文件打开失败"},
    {"POST_TRANSITION_QUARANTINE", "状态切换后隔离期"},
    {"TRANSITION_PENDING", "状态切换尚未完成"},
    {"OPEN_REBOUND_QUARANTINE", "开模回弹隔离期"},
    {"NOT_OPEN_STABLE", "当前不是开模稳定"},
    {"PROFILE_NOT_ACCEPTED", "采集profile未被接受"},
    {"TEMPLATE_COMPATIBILITY_UNKNOWN", "模板兼容性未知"},
    {"NO_CANDIDATE", "没有模板候选"},
    {"LOW_QUALITY", "模板候选质量不足"},
    {"CHANNEL_MISSING", "通道缺失"},
    {"CHANNEL_INCONSISTENT", "通道不一致"},
    {"PAYLOAD_MISSING", "波形载荷缺失"},
    {"PAYLOAD_CRC_MISMATCH", "波形载荷CRC不匹配"},
    {"PAYLOAD_COPY_FAILED", "波形载荷复制失败"},
    {"PAYLOAD_STALE_OR_OVERWRITTEN", "波形载荷陈旧或被覆盖"},
    {"BUFFER_LOCK_FAILED", "缓冲区加锁失败"},
    {"BUFFER_LOCK_TIMEOUT", "缓冲区加锁超时"},
    {"SUMMARY_FINGERPRINT_MISMATCH", "摘要指纹与上下文不一致"},
    {"PERSIST_FAILED", "持久记录失败"},
    {"PERSIST_WARNING", "持久记录警告"},
    {"COMPATIBILITY_FAILED", "兼容性检查失败"},
    {"UNBALANCE_ALARM_TRIGGERED", "不平衡报警已触发"},
    {"UNBALANCE_ALARM_WARN", "不平衡报警预警"},
    {"AGC_WEAK_RESPONSE", "增益调整后信号响应不足"}
}};

} // namespace

QString forceRodReasonText(quint32 code)
{
    return labelFor(code, kForceRodReasons, QStringLiteral("未知逐杆力原因"));
}

QString gateReasonText(quint32 code)
{
    return labelFor(code, kGateReasons, QStringLiteral("未知正式输出原因"));
}

QString diagnosticReasonText(quint32 code)
{
    return labelFor(code, kDiagnosticReasons, QStringLiteral("未知R3事件原因"));
}

QString eventLifecycleText(int code)
{
    static const std::array<const char *, 3> values {
        "RAISED · 新发生", "UPDATED · 持续更新", "RESOLVED · 已恢复"
    };
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("UNKNOWN · %1").arg(code);
}

QString severityText(int code)
{
    static const std::array<const char *, 4> values {
        "INFO · 信息", "WARNING · 警告", "ERROR · 错误", "FATAL · 严重故障"
    };
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("UNKNOWN · %1").arg(code);
}

QString stageText(int code)
{
    static const std::array<const char *, 14> values {
        "会话控制", "外部状态", "摘要解码", "采集质量", "时间差选择",
        "活动上下文", "标定", "几何", "力值管线", "输出策略",
        "采集控制", "过程状态", "就绪状态", "运行健康"
    };
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("未知阶段 %1").arg(code);
}

QString scopeText(int code)
{
    static const std::array<const char *, 7> values {
        "全局", "拉杆", "通道", "上下文", "采集", "几何", "输出"
    };
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("未知范围 %1").arg(code);
}

QString reasonFamilyText(int code)
{
    static const std::array<const char *, 25> values {
        "摘要解码", "T0无效", "NCC峰值核心无效", "采集生产者质量",
        "时间差选择", "上下文未就绪", "上下文不兼容", "标定无效",
        "K0或Kmat无效", "几何参数无效", "部分拉杆", "负力",
        "低于最小载荷", "采集控制", "节拍控制", "过程状态",
        "就绪状态", "运行健康", "输出策略", "模式控制",
        "人工试验控制", "波形模板刷新", "零载参考刷新", "滤波保持",
        "不平衡告警"
    };
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("未知原因族 %1").arg(code);
}

QString normalizedProcessStateText(int code)
{
    static const std::array<const char *, 9> values {
        "UNKNOWN · 未知", "OPENING · 开模中", "OPEN_STABLE · 开模稳定",
        "CLOSING · 合模中", "CONTACT_PRELOAD · 接触预载",
        "PRELOAD_TIMEOUT · 预载超时", "PRESSURE_HOLD · 保压",
        "LOADING · 加载", "RELEASING · 释放"
    };
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("UNKNOWN · %1").arg(code);
}

QString agcLifecycleText(int code)
{
    static const std::array<const char *, 13> values {
        "IDLE · 空闲", "QUALITY_OBSERVING · 质量观察",
        "ADJUSTMENT_RECOMMENDED · 已建议调整",
        "REQUEST_BLOCKED · 请求被阻断", "REQUEST_PENDING · 请求等待",
        "APPLYING · 正在应用", "SETTLING · 等待稳定",
        "FEEDBACK_MATCHED · 反馈匹配", "REJECTED · 已拒绝",
        "FAILED · 已失败", "TIMEOUT · 超时", "COOLDOWN · 冷却期",
        "FROZEN · 已冻结"
    };
    if (code == -1) return QStringLiteral("未提供");
    return code >= 0 && code < static_cast<int>(values.size())
        ? QString::fromUtf8(values[code])
        : QStringLiteral("UNKNOWN · %1").arg(code);
}

} // namespace ucm
