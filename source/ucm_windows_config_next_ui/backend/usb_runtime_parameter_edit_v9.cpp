#include "usb_runtime_parameter_edit_v9.h"

#include <cmath>
#include <limits>

namespace ucm {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool globalOnly(int elementIndex, QString *error)
{
    return elementIndex == -1 || fail(
        error, QStringLiteral("全局运行参数不能携带逐杆索引。"));
}

bool perRodOnly(int elementIndex, QString *error)
{
    return (elementIndex >= 0 && elementIndex < 4) || fail(
        error, QStringLiteral("逐杆运行参数必须指定0到3的杆索引。"));
}

bool descriptorAcceptsValue(const UsbParameterDescriptorV2 &descriptor,
                            qint64 value, QString *error)
{
    const double numeric = static_cast<double>(value);
    if (!std::isfinite(descriptor.minimumValue)
        || !std::isfinite(descriptor.maximumValue)
        || numeric < descriptor.minimumValue
        || numeric > descriptor.maximumValue) {
        return fail(error, QStringLiteral("字段%1的候选值%2超出目录范围。")
            .arg(descriptor.fieldId).arg(value));
    }
    if ((descriptor.constraintFlags & kUsbParameterConstraintStepV2) != 0U) {
        const qint64 step = static_cast<qint64>(descriptor.stepValue);
        const qint64 minimum = static_cast<qint64>(descriptor.minimumValue);
        if (step <= 0 || (value - minimum) % step != 0) {
            return fail(error, QStringLiteral("字段%1的候选值%2不符合步进%3。")
                .arg(descriptor.fieldId).arg(value).arg(step));
        }
    }
    if ((descriptor.constraintFlags & kUsbParameterConstraintEnumMaskV2) != 0U) {
        if (value < 0 || value >= 64
            || (descriptor.enumMask & (1ULL << static_cast<quint64>(value))) == 0U) {
            return fail(error, QStringLiteral("字段%1的候选值%2不在枚举白名单。")
                .arg(descriptor.fieldId).arg(value));
        }
    }
    return true;
}

} // namespace

bool usbRuntimeParameterValueV9(const UsbRuntimeConfigObjectV9 &v,
                                quint32 fieldId, int elementIndex,
                                qint64 *value, QString *error)
{
    if (!value) return fail(error, QStringLiteral("运行参数输出指针为空。"));
    if (fieldId == 16U || fieldId == 17U) {
        if (!perRodOnly(elementIndex, error)) return false;
    } else if (!globalOnly(elementIndex, error)) {
        return false;
    }
    switch (fieldId) {
    case 16U: *value = v.lnaGainDb[elementIndex]; break;
    case 17U: *value = v.digitalGainSteps[elementIndex]; break;
    case 18U: *value = v.pgaGainDb; break;
    case 19U: *value = v.vcntlDacCode; break;
    case 20U: *value = v.digitalTgcAttenuationDb; break;
    case 64U: *value = v.minNccPeakMillionths; break;
    case 65U: *value = v.minNccPeakRatioMillionths; break;
    case 66U: *value = v.minSnrMilliDb; break;
    case 80U: *value = v.templateConfirmFrames; break;
    case 81U: *value = v.minimumValidForceN; break;
    case 82U: *value = v.biasThresholdMillionths; break;
    case UsbParameterBiasLowLoadGateNV2: *value = v.biasLowLoadGateN; break;
    case 83U: *value = v.alarmConfirmFrames; break;
    case UsbParameterMeasurementHzV2: *value = v.measurementHz; break;
    case UsbParameterNominalHvVoltsV2: *value = v.nominalHvVolts; break;
    case UsbParameterTxBurstCyclesV2: *value = v.txBurstCycles; break;
    case UsbParameterCaptureWindowStartV2: *value = v.captureWindowStart; break;
    case UsbParameterAgcEnabledV2:
        *value = (v.agcFlags & kUsbRuntimeAgcEnabledV9) != 0U ? 1 : 0;
        break;
    case UsbParameterAgcPeakLowPermilleV2: *value = v.agcPeakLowPermille; break;
    case UsbParameterAgcPeakHighPermilleV2: *value = v.agcPeakHighPermille; break;
    case UsbParameterAgcEmergencyPermilleV2: *value = v.agcEmergencyPermille; break;
    case UsbParameterAgcMaxDelayJitterNsV2: *value = v.agcMaxDelayJitterPs; break;
    case UsbParameterAgcMinValidRateV2: *value = v.agcMinimumValidRateMillionths; break;
    case UsbParameterAgcMaxClippingRateV2: *value = v.agcMaximumClippingRateMillionths; break;
    case UsbParameterAgcConfirmFramesV2: *value = v.agcConfirmFrames; break;
    case UsbParameterAgcSettleFramesV2: *value = v.agcSettleFrames; break;
    case UsbParameterAgcMaxAdjustmentsV2: *value = v.agcMaximumAdjustments; break;
    case UsbParameterAgcVcntlMinimumV2: *value = v.agcVcntlMinimum; break;
    case UsbParameterAgcVcntlMaximumV2: *value = v.agcVcntlMaximum; break;
    case UsbParameterAgcVcntlFineStepV2: *value = v.agcVcntlFineStep; break;
    case UsbParameterAgcVcntlMediumStepV2: *value = v.agcVcntlMediumStep; break;
    case UsbParameterAgcVcntlCoarseStepV2: *value = v.agcVcntlCoarseStep; break;
    case UsbParameterAgcPgaAllowedMaskV2: *value = v.agcPgaAllowedMask; break;
    case UsbParameterAgcLnaAllowedMaskV2: *value = v.agcLnaAllowedMask; break;
    case UsbParameterAgcHvMinimumV2: *value = v.agcHvMinimumVolts; break;
    case UsbParameterAgcHvMaximumV2: *value = v.agcHvMaximumVolts; break;
    case UsbParameterAgcHvStepV2: *value = v.agcHvStepVolts; break;
    case UsbParameterAgcBurstMinimumV2: *value = v.agcBurstMinimum; break;
    case UsbParameterAgcBurstMaximumV2: *value = v.agcBurstMaximum; break;
    default:
        return fail(error, QStringLiteral("字段%1不属于CFG2运行配置。")
            .arg(fieldId));
    }
    return true;
}

bool applyUsbRuntimeParameterEditV9(
    UsbRuntimeConfigObjectV9 *v,
    const UsbParameterDescriptorV2 &descriptor,
    quint64 activeConfigGroupMask,
    const UsbRuntimeParameterEditV9 &edit,
    QString *error)
{
    if (!v || descriptor.fieldId != edit.fieldId)
        return fail(error, QStringLiteral("运行参数编辑与目录字段不匹配。"));
    if (edit.fieldId == 17U || edit.fieldId == 20U) {
        return fail(error, QStringLiteral(
            "数字增益与数字TGC在产品模式固定为0，仅提供只读诊断。"));
    }
    if (!usbParameterEffectivelyWritableV2(descriptor,
                                           activeConfigGroupMask)
        || usbParameterWriteChannelV2(edit.fieldId)
            != UsbParameterWriteChannelV2::RuntimeConfiguration
        || (descriptor.accessFlags & kUsbParameterAccessFrozenV2) != 0U) {
        return fail(error, QStringLiteral("字段%1当前不是可写运行参数。")
            .arg(edit.fieldId));
    }
    if ((descriptor.scope == 2U && !perRodOnly(edit.elementIndex, error))
        || (descriptor.scope != 2U && !globalOnly(edit.elementIndex, error))
        || !descriptorAcceptsValue(descriptor, edit.value, error)) {
        return false;
    }
    const quint32 u = static_cast<quint32>(edit.value);
    switch (edit.fieldId) {
    case 16U: v->lnaGainDb[edit.elementIndex] = u; break;
    case 17U: v->digitalGainSteps[edit.elementIndex] = u; break;
    case 18U: v->pgaGainDb = u; break;
    case 19U: v->vcntlDacCode = u; break;
    case 20U: v->digitalTgcAttenuationDb = u; break;
    case 64U: v->minNccPeakMillionths = u; break;
    case 65U: v->minNccPeakRatioMillionths = u; break;
    case 66U: v->minSnrMilliDb = static_cast<qint32>(edit.value); break;
    case 80U: v->templateConfirmFrames = u; break;
    case 81U: v->minimumValidForceN = u; break;
    case 82U: v->biasThresholdMillionths = u; break;
    case 83U: v->alarmConfirmFrames = u; break;
    case UsbParameterBiasLowLoadGateNV2: v->biasLowLoadGateN = u; break;
    case UsbParameterMeasurementHzV2: v->measurementHz = u; break;
    case UsbParameterNominalHvVoltsV2: v->nominalHvVolts = u; break;
    case UsbParameterTxBurstCyclesV2: v->txBurstCycles = u; break;
    case UsbParameterCaptureWindowStartV2: v->captureWindowStart = u; break;
    case UsbParameterAgcEnabledV2:
        if (u != 0U) v->agcFlags |= kUsbRuntimeAgcEnabledV9;
        else v->agcFlags &= ~kUsbRuntimeAgcEnabledV9;
        break;
    case UsbParameterAgcPeakLowPermilleV2: v->agcPeakLowPermille = u; break;
    case UsbParameterAgcPeakHighPermilleV2: v->agcPeakHighPermille = u; break;
    case UsbParameterAgcEmergencyPermilleV2: v->agcEmergencyPermille = u; break;
    case UsbParameterAgcMaxDelayJitterNsV2: v->agcMaxDelayJitterPs = u; break;
    case UsbParameterAgcMinValidRateV2: v->agcMinimumValidRateMillionths = u; break;
    case UsbParameterAgcMaxClippingRateV2: v->agcMaximumClippingRateMillionths = u; break;
    case UsbParameterAgcConfirmFramesV2: v->agcConfirmFrames = u; break;
    case UsbParameterAgcSettleFramesV2: v->agcSettleFrames = u; break;
    case UsbParameterAgcMaxAdjustmentsV2: v->agcMaximumAdjustments = u; break;
    case UsbParameterAgcVcntlMinimumV2: v->agcVcntlMinimum = u; break;
    case UsbParameterAgcVcntlMaximumV2: v->agcVcntlMaximum = u; break;
    case UsbParameterAgcVcntlFineStepV2: v->agcVcntlFineStep = u; break;
    case UsbParameterAgcVcntlMediumStepV2: v->agcVcntlMediumStep = u; break;
    case UsbParameterAgcVcntlCoarseStepV2: v->agcVcntlCoarseStep = u; break;
    case UsbParameterAgcPgaAllowedMaskV2: v->agcPgaAllowedMask = u; break;
    case UsbParameterAgcLnaAllowedMaskV2: v->agcLnaAllowedMask = u; break;
    case UsbParameterAgcHvMinimumV2: v->agcHvMinimumVolts = u; break;
    case UsbParameterAgcHvMaximumV2: v->agcHvMaximumVolts = u; break;
    case UsbParameterAgcHvStepV2: v->agcHvStepVolts = u; break;
    case UsbParameterAgcBurstMinimumV2: v->agcBurstMinimum = u; break;
    case UsbParameterAgcBurstMaximumV2: v->agcBurstMaximum = u; break;
    default:
        return fail(error, QStringLiteral("字段%1尚未接入CFG2写入映射。")
            .arg(edit.fieldId));
    }
    return true;
}

bool verifyUsbRuntimeParameterReadbackV9(
    const UsbRuntimeConfigReceiptV9 &receipt,
    const QVector<UsbRuntimeParameterEditV9> &edits,
    QString *error)
{
    if ((receipt.hardwareFlags & kUsbRuntimeHardwareStateValidV9) == 0U)
        return fail(error, QStringLiteral("ARM终态未提供有效UHW2硬件回读。"));
    for (const auto &edit : edits) {
        qint64 activeValue = 0;
        if (!usbRuntimeParameterValueV9(receipt.activeConfiguration,
                                        edit.fieldId, edit.elementIndex,
                                        &activeValue, error)
            || activeValue != edit.value) {
            return fail(error, QStringLiteral("字段%1的活动配置回读不等于候选值。")
                .arg(edit.fieldId));
        }
        qint64 actual = edit.value;
        bool hasHardwareMirror = true;
        switch (edit.fieldId) {
        case 16U: actual = receipt.actualLnaGainDb[edit.elementIndex]; break;
        case 17U: actual = receipt.actualDigitalGainSteps[edit.elementIndex]; break;
        case 18U: actual = receipt.actualPgaGainDb; break;
        case 19U: actual = receipt.actualVcntlDacCode; break;
        case 20U: actual = receipt.actualDigitalTgcAttenuationDb; break;
        case UsbParameterMeasurementHzV2: actual = receipt.actualMeasurementHz; break;
        case UsbParameterNominalHvVoltsV2: actual = receipt.actualNominalHvVolts; break;
        case UsbParameterTxBurstCyclesV2: actual = receipt.actualTxBurstCycles; break;
        default: hasHardwareMirror = false; break;
        }
        if (hasHardwareMirror && actual != edit.value) {
            return fail(error, QStringLiteral("字段%1的硬件实际值不等于候选值。")
                .arg(edit.fieldId));
        }
    }
    return true;
}

bool usbRuntimeConfigurationValuesEqualV9(
    const UsbRuntimeConfigObjectV9 &a, const UsbRuntimeConfigObjectV9 &b)
{
    const QVector<quint32> globals {
        18U, 19U, 20U, 64U, 65U, 66U, 80U, 81U, 82U, 83U,
        UsbParameterBiasLowLoadGateNV2, UsbParameterMeasurementHzV2,
        UsbParameterNominalHvVoltsV2, UsbParameterTxBurstCyclesV2,
        UsbParameterCaptureWindowStartV2,
        UsbParameterAgcEnabledV2, UsbParameterAgcPeakLowPermilleV2,
        UsbParameterAgcPeakHighPermilleV2, UsbParameterAgcEmergencyPermilleV2,
        UsbParameterAgcMaxDelayJitterNsV2, UsbParameterAgcMinValidRateV2,
        UsbParameterAgcMaxClippingRateV2, UsbParameterAgcConfirmFramesV2,
        UsbParameterAgcSettleFramesV2, UsbParameterAgcMaxAdjustmentsV2,
        UsbParameterAgcVcntlMinimumV2, UsbParameterAgcVcntlMaximumV2,
        UsbParameterAgcVcntlFineStepV2, UsbParameterAgcVcntlMediumStepV2,
        UsbParameterAgcVcntlCoarseStepV2, UsbParameterAgcPgaAllowedMaskV2,
        UsbParameterAgcLnaAllowedMaskV2, UsbParameterAgcHvMinimumV2,
        UsbParameterAgcHvMaximumV2, UsbParameterAgcHvStepV2,
        UsbParameterAgcBurstMinimumV2, UsbParameterAgcBurstMaximumV2
    };
    for (quint32 id : globals) {
        qint64 left = 0, right = 0;
        if (!usbRuntimeParameterValueV9(a, id, -1, &left)
            || !usbRuntimeParameterValueV9(b, id, -1, &right)
            || left != right) return false;
    }
    for (int rod = 0; rod < 4; ++rod) {
        for (quint32 id : {16U, 17U}) {
            qint64 left = 0, right = 0;
            if (!usbRuntimeParameterValueV9(a, id, rod, &left)
                || !usbRuntimeParameterValueV9(b, id, rod, &right)
                || left != right) return false;
        }
    }
    return true;
}

} // namespace ucm
