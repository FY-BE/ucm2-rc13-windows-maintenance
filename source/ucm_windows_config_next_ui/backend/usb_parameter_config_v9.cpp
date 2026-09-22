#include "usb_parameter_config_v9.h"

#include "usb_wire_v1.h"

#include <QCryptographicHash>
#include <QtEndian>

#include <cstring>

namespace ucm {
namespace {

quint16 u16(const QByteArray &b, int o) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + o)); }
quint32 u32(const QByteArray &b, int o) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + o)); }
qint32 i32(const QByteArray &b, int o) { return static_cast<qint32>(u32(b, o)); }
quint64 u64(const QByteArray &b, int o) { return qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(b.constData() + o)); }
void p16(QByteArray *b, int o, quint16 v) { v = qToLittleEndian(v); memcpy(b->data() + o, &v, 2); }
void p32(QByteArray *b, int o, quint32 v) { v = qToLittleEndian(v); memcpy(b->data() + o, &v, 4); }
void p64(QByteArray *b, int o, quint64 v) { v = qToLittleEndian(v); memcpy(b->data() + o, &v, 8); }
bool zeros(const QByteArray &b) { for (char c : b) if (c != 0) return false; return true; }
bool lnaValid(quint32 v) { return v == 12U || v == 18U || v == 24U; }
bool fail(QString *error, const QString &message);

bool decodeHardwareState(const QByteArray &b, UsbRuntimeConfigReceiptV9 *v,
                         QString *error)
{
    if (b.size() != kUsbRuntimeHardwareStateBytesV9
        || u32(b, 0) != kUsbRuntimeHardwareStateTokenV9
        || u16(b, 4) != kUsbRuntimeConfigSchemaV9
        || u16(b, 6) != kUsbRuntimeHardwareStateBytesV9
        || (u32(b, 8) & ~(kUsbRuntimeHardwareStateValidV9
                          | kUsbRuntimeHardwareStateLoadedV9)) != 0U
        || !zeros(b.mid(108, 4))) {
        return fail(error, QStringLiteral("UHW2运行硬件回读头、标志或保留字段无效。"));
    }
    v->hardwareFlags = u32(b, 8);
    if ((v->hardwareFlags & kUsbRuntimeHardwareStateValidV9) == 0U) {
        if (v->hardwareFlags != 0U || !zeros(b.mid(12, 100)))
            return fail(error, QStringLiteral("UHW2无效态必须清零全部运行字段。"));
        v->hardwareReceipt = b;
        return true;
    }
    v->agcStage = u32(b, 12);
    v->actualMeasurementHz = u32(b, 16);
    v->actualNominalHvVolts = u32(b, 20);
    v->actualTxBurstCycles = u32(b, 24);
    v->actualPgaGainDb = u32(b, 28);
    v->actualVcntlDacCode = u32(b, 32);
    v->actualDigitalTgcAttenuationDb = u32(b, 36);
    for (int n = 0; n < 4; ++n) v->actualLnaGainDb[n] = u32(b, 40 + n * 4);
    for (int n = 0; n < 4; ++n) v->actualDigitalGainSteps[n] = u32(b, 56 + n * 4);
    v->actualValidChannelMask = u32(b, 72);
    v->hardwareResult = i32(b, 76);
    v->hardwarePublishedMonotonicNs = u64(b, 80);
    v->hardwareActionSequence = u64(b, 88);
    v->hardwareGeneration = u64(b, 96);
    v->agcReason = u32(b, 104);
    if (v->agcStage > kUsbRuntimeAgcStageMaximumV9
        || v->agcReason > kUsbRuntimeAgcReasonMaximumV9
        || v->actualMeasurementHz != 50U
        || v->actualNominalHvVolts < 50U || v->actualNominalHvVolts > 250U
        || (v->actualNominalHvVolts % 5U) != 0U
        || v->actualTxBurstCycles < 1U || v->actualTxBurstCycles > 8U
        || (v->actualPgaGainDb != 24U && v->actualPgaGainDb != 30U)
        || v->actualVcntlDacCode < kUsbRuntimeVcntlDacMinimumV9
        || v->actualVcntlDacCode > kUsbRuntimeVcntlDacMaximumV9
        || v->actualDigitalTgcAttenuationDb > 42U
        || (v->actualDigitalTgcAttenuationDb % 6U) != 0U
        || (v->actualValidChannelMask & ~0x0FU) != 0U
        || v->hardwareResult < 0 || v->hardwareResult > 12
        || v->hardwarePublishedMonotonicNs == 0U
        || v->hardwareActionSequence == 0U || v->hardwareGeneration == 0U) {
        return fail(error, QStringLiteral("UHW2运行硬件回读字段越界。"));
    }
    for (quint32 value : v->actualLnaGainDb)
        if (!lnaValid(value)) return fail(error, QStringLiteral("UHW2逐杆LNA回读无效。"));
    for (quint32 value : v->actualDigitalGainSteps)
        if (value > 30U) return fail(error, QStringLiteral("UHW2逐杆数字增益回读无效。"));
    v->hardwareReceipt = b;
    return true;
}

void writeFields(const UsbRuntimeConfigObjectV9 &v, QByteArray *b)
{
    p32(b, 0, kUsbRuntimeConfigTokenV9);
    p16(b, 4, kUsbRuntimeConfigSchemaV9);
    p16(b, 6, kUsbRuntimeConfigObjectBytesV9);
    p32(b, 12, v.flags);
    p32(b, 16, v.operation);
    p32(b, 20, kUsbRuntimeConfigObjectRevisionV9);
    p64(b, 24, v.transactionId);
    p64(b, 32, v.baseGeneration);
    p64(b, 40, v.candidateGeneration);
    p64(b, 48, v.presentGroupMask);
    p64(b, 56, v.changedGroupMask);
    p32(b, 64, v.parameterCatalogCrc32);
    p32(b, 68, v.deviceProfileCrc32);
    p32(b, 112, v.deviceModel);
    p32(b, 116, v.measurementHz);
    p32(b, 120, v.rodLengthMm);
    p32(b, 124, v.measurementPointMm);
    p32(b, 128, v.longitudinalVelocityMps);
    p32(b, 132, v.nominalHvVolts);
    p32(b, 136, v.txBurstCycles);
    p32(b, 140, v.pgaGainDb);
    p32(b, 144, v.vcntlDacCode);
    p32(b, 148, v.digitalTgcAttenuationDb);
    p32(b, 152, v.minNccPeakMillionths);
    p32(b, 156, v.minNccPeakRatioMillionths);
    p32(b, 160, static_cast<quint32>(v.minSnrMilliDb));
    p32(b, 164, v.templateConfirmFrames);
    p32(b, 168, v.minimumValidForceN);
    p32(b, 172, v.biasThresholdMillionths);
    p32(b, 176, v.biasLowLoadGateN);
    p32(b, 180, v.alarmConfirmFrames);
    p32(b, 184, v.agcFlags);
    p32(b, 188, v.agcPeakLowPermille);
    p32(b, 192, v.agcPeakHighPermille);
    p32(b, 196, v.agcEmergencyPermille);
    p32(b, 200, v.agcMaxDelayJitterPs);
    p32(b, 204, v.agcMinimumValidRateMillionths);
    p32(b, 208, v.agcMaximumClippingRateMillionths);
    p32(b, 212, v.agcConfirmFrames);
    p32(b, 216, v.agcSettleFrames);
    p32(b, 220, v.agcMaximumAdjustments);
    p32(b, 224, v.agcVcntlMinimum);
    p32(b, 228, v.agcVcntlMaximum);
    p32(b, 232, v.agcVcntlFineStep);
    p32(b, 236, v.agcVcntlMediumStep);
    p32(b, 240, v.agcVcntlCoarseStep);
    p32(b, 244, v.agcHvMinimumVolts);
    p32(b, 248, v.agcHvMaximumVolts);
    p32(b, 252, v.agcHvStepVolts);
    p32(b, 256, v.agcBurstMinimum);
    p32(b, 260, v.agcBurstMaximum);
    for (int n = 0; n < 4; ++n) p32(b, 264 + n * 4, v.lnaGainDb[n]);
    for (int n = 0; n < 4; ++n) p32(b, 280 + n * 4, v.digitalGainSteps[n]);
    p32(b, 296, v.agcPgaAllowedMask);
    p32(b, 300, v.agcLnaAllowedMask);
    p32(b, 304, v.captureWindowStart);
}

void readFields(const QByteArray &b, UsbRuntimeConfigObjectV9 *v)
{
    v->flags = u32(b, 12); v->operation = u32(b, 16);
    v->transactionId = u64(b, 24); v->baseGeneration = u64(b, 32);
    v->candidateGeneration = u64(b, 40); v->presentGroupMask = u64(b, 48);
    v->changedGroupMask = u64(b, 56); v->parameterCatalogCrc32 = u32(b, 64);
    v->deviceProfileCrc32 = u32(b, 68); v->objectSha256 = b.mid(72, 32);
    v->deviceModel = u32(b, 112); v->measurementHz = u32(b, 116);
    v->rodLengthMm = u32(b, 120); v->measurementPointMm = u32(b, 124);
    v->longitudinalVelocityMps = u32(b, 128); v->nominalHvVolts = u32(b, 132);
    v->txBurstCycles = u32(b, 136); v->captureWindowStart = u32(b, 304);
    v->pgaGainDb = u32(b, 140);
    v->vcntlDacCode = u32(b, 144); v->digitalTgcAttenuationDb = u32(b, 148);
    v->minNccPeakMillionths = u32(b, 152); v->minNccPeakRatioMillionths = u32(b, 156);
    v->minSnrMilliDb = i32(b, 160); v->templateConfirmFrames = u32(b, 164);
    v->minimumValidForceN = u32(b, 168); v->biasThresholdMillionths = u32(b, 172);
    v->biasLowLoadGateN = u32(b, 176); v->alarmConfirmFrames = u32(b, 180);
    v->agcFlags = u32(b, 184); v->agcPeakLowPermille = u32(b, 188);
    v->agcPeakHighPermille = u32(b, 192); v->agcEmergencyPermille = u32(b, 196);
    v->agcMaxDelayJitterPs = u32(b, 200); v->agcMinimumValidRateMillionths = u32(b, 204);
    v->agcMaximumClippingRateMillionths = u32(b, 208); v->agcConfirmFrames = u32(b, 212);
    v->agcSettleFrames = u32(b, 216); v->agcMaximumAdjustments = u32(b, 220);
    v->agcVcntlMinimum = u32(b, 224); v->agcVcntlMaximum = u32(b, 228);
    v->agcVcntlFineStep = u32(b, 232); v->agcVcntlMediumStep = u32(b, 236);
    v->agcVcntlCoarseStep = u32(b, 240); v->agcHvMinimumVolts = u32(b, 244);
    v->agcHvMaximumVolts = u32(b, 248); v->agcHvStepVolts = u32(b, 252);
    v->agcBurstMinimum = u32(b, 256); v->agcBurstMaximum = u32(b, 260);
    for (int n = 0; n < 4; ++n) v->lnaGainDb[n] = u32(b, 264 + n * 4);
    for (int n = 0; n < 4; ++n) v->digitalGainSteps[n] = u32(b, 280 + n * 4);
    v->agcPgaAllowedMask = u32(b, 296); v->agcLnaAllowedMask = u32(b, 300);
    v->encoded = b;
}

bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }

} // namespace

bool validateUsbRuntimeConfigObjectV9(const UsbRuntimeConfigObjectV9 &v, QString *error)
{
    const bool candidateOperation = v.operation == UsbRuntimeConfigApplyV9
        || v.operation == UsbRuntimeConfigValidateV9;
    const bool generationValid = candidateOperation
        ? v.candidateGeneration == v.baseGeneration + 1U
            && v.changedGroupMask != 0U
        : v.operation == UsbRuntimeConfigSaveStartupV9
            && v.candidateGeneration == v.baseGeneration
            && v.changedGroupMask == 0U;
    constexpr quint32 safetyOrder = kUsbRuntimeAgcFreezeWhenLoadedV9
        | kUsbRuntimeAgcReceiveBeforeHvV9 | kUsbRuntimeAgcHvBeforeBurstV9;
    if (v.flags != 0U || v.transactionId == 0U || v.baseGeneration == 0U
        || !generationValid || v.presentGroupMask == 0U
        || (v.presentGroupMask & ~0x7FFULL) != 0U
        || (v.changedGroupMask & ~v.presentGroupMask) != 0U
        || v.parameterCatalogCrc32 == 0U || v.deviceProfileCrc32 == 0U
        || (v.deviceModel != 1U && v.deviceModel != 2U && v.deviceModel != 3U)
        || v.measurementHz != 50U || (v.deviceModel != 3U && v.rodLengthMm == 0U)
        || v.measurementPointMm > v.rodLengthMm
        || v.longitudinalVelocityMps == 0U
        || v.nominalHvVolts < 50U || v.nominalHvVolts > 250U
        || (v.nominalHvVolts % 5U) != 0U
        || v.txBurstCycles < 1U || v.txBurstCycles > 8U
        || v.captureWindowStart > 91808U
        || (v.pgaGainDb != 24U && v.pgaGainDb != 30U)
        || v.vcntlDacCode < kUsbRuntimeVcntlDacMinimumV9
        || v.vcntlDacCode > kUsbRuntimeVcntlDacMaximumV9
        || v.digitalTgcAttenuationDb != 0U
        || v.minNccPeakMillionths > 1000000U
        || v.minNccPeakRatioMillionths < 1000000U
        || v.minNccPeakRatioMillionths > 10000000U
        || v.minSnrMilliDb < -100000 || v.minSnrMilliDb > 100000
        || v.templateConfirmFrames == 0U || v.alarmConfirmFrames == 0U
        || v.biasThresholdMillionths > 1000000U
        || (v.agcFlags & safetyOrder) != safetyOrder
        || (v.agcFlags & ~kUsbRuntimeAgcFlagsRequiredV9) != 0U
        || v.agcPeakLowPermille == 0U
        || v.agcPeakLowPermille >= v.agcPeakHighPermille
        || v.agcPeakHighPermille >= v.agcEmergencyPermille
        || v.agcEmergencyPermille > 1000U
        || v.agcMinimumValidRateMillionths > 1000000U
        || v.agcMaximumClippingRateMillionths > 1000000U
        || v.agcConfirmFrames == 0U || v.agcSettleFrames == 0U
        || v.agcMaximumAdjustments == 0U
        || v.agcVcntlMinimum < kUsbRuntimeVcntlDacMinimumV9
        || v.agcVcntlMinimum > v.agcVcntlMaximum
        || v.agcVcntlMaximum > kUsbRuntimeVcntlDacMaximumV9
        || v.agcVcntlFineStep == 0U
        || v.agcVcntlFineStep > v.agcVcntlMediumStep
        || v.agcVcntlMediumStep > v.agcVcntlCoarseStep
        || v.agcHvMinimumVolts < 50U || v.agcHvMaximumVolts > 250U
        || v.agcHvMinimumVolts > v.agcHvMaximumVolts
        || v.agcHvStepVolts != 5U
        || v.nominalHvVolts < v.agcHvMinimumVolts
        || v.nominalHvVolts > v.agcHvMaximumVolts
        || v.agcBurstMinimum < 1U || v.agcBurstMaximum > 8U
        || v.agcBurstMinimum > v.agcBurstMaximum
        || v.txBurstCycles < v.agcBurstMinimum
        || v.txBurstCycles > v.agcBurstMaximum
        || v.agcPgaAllowedMask == 0U || (v.agcPgaAllowedMask & ~0x3U) != 0U
        || v.agcLnaAllowedMask == 0U || (v.agcLnaAllowedMask & ~0x7U) != 0U)
        return fail(error, QStringLiteral("CFG2 revision 4字段、代际或AGC安全顺序无效。"));
    for (quint32 value : v.lnaGainDb)
        if (!lnaValid(value)) return fail(error, QStringLiteral("CFG2逐杆LNA档位无效。"));
    for (quint32 value : v.digitalGainSteps)
        if (value != 0U) return fail(error, QStringLiteral(
            "CFG2产品模式逐杆数字增益必须固定为0。"));
    return true;
}

QByteArray encodeUsbRuntimeConfigObjectV9(const UsbRuntimeConfigObjectV9 &v, QString *error)
{
    if (!validateUsbRuntimeConfigObjectV9(v, error)) return {};
    QByteArray bytes(kUsbRuntimeConfigObjectBytesV9, 0);
    writeFields(v, &bytes);
    QByteArray hashImage = bytes;
    memset(hashImage.data() + 8, 0, 4);
    memset(hashImage.data() + 72, 0, 32);
    const QByteArray sha = QCryptographicHash::hash(hashImage, QCryptographicHash::Sha256);
    memcpy(bytes.data() + 72, sha.constData(), 32);
    QByteArray crcImage = bytes;
    memset(crcImage.data() + 8, 0, 4);
    p32(&bytes, 8, usbCrc32V1(crcImage));
    return bytes;
}

bool decodeUsbRuntimeConfigObjectV9(const QByteArray &payload,
                                    UsbRuntimeConfigObjectV9 *object,
                                    QString *error,
                                    quint32 expectedCatalogCrc32)
{
    if (!object || payload.size() != kUsbRuntimeConfigObjectBytesV9
        || u32(payload, 0) != kUsbRuntimeConfigTokenV9
        || u16(payload, 4) != kUsbRuntimeConfigSchemaV9
        || u16(payload, 6) != kUsbRuntimeConfigObjectBytesV9
        || u32(payload, 20) != kUsbRuntimeConfigObjectRevisionV9)
        return fail(error, QStringLiteral("CFG2 revision 4对象头或长度无效。"));
    QByteArray crcImage = payload;
    const quint32 expectedCrc = u32(payload, 8);
    memset(crcImage.data() + 8, 0, 4);
    if (expectedCrc == 0U || usbCrc32V1(crcImage) != expectedCrc)
        return fail(error, QStringLiteral("CFG2 revision 4 CRC无效。"));
    QByteArray hashImage = payload;
    memset(hashImage.data() + 8, 0, 4);
    memset(hashImage.data() + 72, 0, 32);
    if (QCryptographicHash::hash(hashImage, QCryptographicHash::Sha256)
            != payload.mid(72, 32)
        || !zeros(payload.mid(104, 8)) || !zeros(payload.mid(308, 76)))
        return fail(error, QStringLiteral("CFG2 revision 4 SHA或保留字段无效。"));
    UsbRuntimeConfigObjectV9 decoded;
    readFields(payload, &decoded);
    if ((expectedCatalogCrc32 != 0U
         && decoded.parameterCatalogCrc32 != expectedCatalogCrc32)
        || !validateUsbRuntimeConfigObjectV9(decoded, error)) return false;
    *object = decoded;
    return true;
}

QByteArray encodeUsbRuntimeConfigQueryV9(quint32 kind, quint64 transactionId,
                                         QString *error)
{
    if (kind < UsbRuntimeConfigQueryLatestV9
        || kind > UsbRuntimeConfigQueryTransactionV9
        || (kind == UsbRuntimeConfigQueryTransactionV9) != (transactionId != 0U)) {
        fail(error, QStringLiteral("UCQ2查询类型与transaction_id不匹配。"));
        return {};
    }
    QByteArray bytes(kUsbRuntimeConfigQueryBytesV9, 0);
    p32(&bytes, 0, kUsbRuntimeConfigQueryTokenV9);
    p16(&bytes, 4, kUsbRuntimeConfigSchemaV9);
    p16(&bytes, 6, kUsbRuntimeConfigQueryBytesV9);
    p32(&bytes, 12, kind);
    p64(&bytes, 16, transactionId);
    QByteArray crcImage = bytes;
    p32(&bytes, 8, usbCrc32V1(crcImage));
    return bytes;
}

bool decodeUsbRuntimeConfigReceiptV9(const QByteArray &payload,
                                     UsbRuntimeConfigReceiptV9 *receipt,
                                     QString *error,
                                     quint32 expectedCatalogCrc32)
{
    if (!receipt || payload.size() != kUsbRuntimeConfigReceiptBytesV9
        || u32(payload, 0) != kUsbRuntimeConfigReceiptTokenV9
        || u16(payload, 4) != kUsbRuntimeConfigSchemaV9
        || u16(payload, 6) != kUsbRuntimeConfigReceiptBytesV9)
        return fail(error, QStringLiteral("UCR2 revision 4回执头或长度无效。"));
    QByteArray crcImage = payload;
    const quint32 expectedCrc = u32(payload, 8);
    memset(crcImage.data() + 8, 0, 4);
    if (expectedCrc == 0U || usbCrc32V1(crcImage) != expectedCrc
        || u32(payload, 28) != kUsbRuntimeConfigFieldResultCountV9
        || u32(payload, 72) != 0U || u32(payload, 76) != 0U)
        return fail(error, QStringLiteral("UCR2 revision 4 CRC、结果数或保留字段无效。"));
    UsbRuntimeConfigReceiptV9 decoded;
    decoded.kind = u32(payload, 12); decoded.result = i32(payload, 16);
    decoded.persisted = u32(payload, 20) != 0U; decoded.operation = u32(payload, 24);
    decoded.transactionId = u64(payload, 32); decoded.baseGeneration = u64(payload, 40);
    decoded.requestedGeneration = u64(payload, 48); decoded.activeGeneration = u64(payload, 56);
    decoded.changedGroupMask = u64(payload, 64);
    if (!decodeUsbRuntimeConfigObjectV9(payload.mid(80, kUsbRuntimeConfigObjectBytesV9),
                                        &decoded.activeConfiguration, error,
                                        expectedCatalogCrc32)) return false;
    for (int n = 0; n < kUsbRuntimeConfigFieldResultCountV9; ++n)
        decoded.fieldResults[n] = i32(payload, 464 + n * 4);
    if (!decodeHardwareState(payload.mid(656, kUsbRuntimeHardwareStateBytesV9),
                             &decoded, error)) return false;
    decoded.encoded = payload;
    const bool kindValid =
        (decoded.kind == UsbRuntimeConfigReceiptAppliedV9
         && decoded.result == 0 && decoded.operation == UsbRuntimeConfigApplyV9
         && !decoded.persisted)
        || (decoded.kind == UsbRuntimeConfigReceiptValidatedV9
            && decoded.result == 0 && decoded.operation == UsbRuntimeConfigValidateV9
            && !decoded.persisted)
        || (decoded.kind == UsbRuntimeConfigReceiptSavedStartupV9
            && decoded.result == 0 && decoded.operation == UsbRuntimeConfigSaveStartupV9
            && decoded.persisted)
        || (decoded.kind == UsbRuntimeConfigReceiptRejectedV9
            && decoded.result != 0)
        || (decoded.kind == UsbRuntimeConfigReceiptAcceptedV9
            && decoded.result == 11 && !decoded.persisted)
        || (decoded.kind == UsbRuntimeConfigReceiptActiveV9
            && decoded.result == 0);
    if (!kindValid || decoded.transactionId == 0U
        || decoded.activeGeneration == 0U)
        return fail(error, QStringLiteral("UCR2 revision 4终态语义无效。"));
    for (qint32 value : decoded.fieldResults)
        if (value < 0 || value > 12)
            return fail(error, QStringLiteral("UCR2 revision 4字段结果越界。"));
    *receipt = decoded;
    return true;
}

QString usbRuntimeAgcStageTextV9(quint32 stage)
{
    static const char *const names[] = {
        "禁用", "等待卸载", "LNA扫描", "PGA扫描", "VCNTL粗扫",
        "VCNTL细扫", "HV扫描", "Burst扫描", "最终确认", "运行跟踪",
        "等待持久化", "已持久化", "故障"
    };
    if (stage > kUsbRuntimeAgcStageMaximumV9)
        return QStringLiteral("未知阶段%1").arg(stage);
    return QString::fromUtf8(names[stage]);
}

QString usbRuntimeAgcReasonTextV9(quint32 reason)
{
    static const char *const names[] = {
        "无", "等待波形确认卸载", "PLC过渡", "PLC受力",
        "候选被拒绝", "没有合格工作点", "应用失败", "最终确认失败",
        "扫描完成", "等待稳定周期", "可持久化", "已持久化"
    };
    if (reason > kUsbRuntimeAgcReasonMaximumV9)
        return QStringLiteral("未知原因%1").arg(reason);
    return QString::fromUtf8(names[reason]);
}

} // namespace ucm
