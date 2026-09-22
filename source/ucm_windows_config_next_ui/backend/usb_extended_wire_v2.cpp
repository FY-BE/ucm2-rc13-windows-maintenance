#include "usb_extended_wire_v2.h"
#include "usb_parameter_config_v9.h"
#include "usb_wire_v1.h"

#include <QtEndian>
#include <QStringList>

#include <cmath>
#include <cstring>

namespace ucm {
namespace {

constexpr quint32 kExtendedFlagsAll = 0x000000FEU;
constexpr quint32 kAccessFlagsAll = 0x0000007FU;
constexpr quint32 kConstraintFlagsAll = 0x0000001FU;
constexpr quint32 kCatalogMore = 1U << 0;

void appendU16(QByteArray &bytes, quint16 value)
{
    const quint16 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), sizeof(little));
}

void appendU32(QByteArray &bytes, quint32 value)
{
    const quint32 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), sizeof(little));
}

void appendU64(QByteArray &bytes, quint64 value)
{
    const quint64 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), sizeof(little));
}

quint16 readU16(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

qint32 readI32(const QByteArray &bytes, int offset)
{
    return static_cast<qint32>(readU32(bytes, offset));
}

quint64 readU64(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

double readF64(const QByteArray &bytes, int offset)
{
    const quint64 bits = readU64(bytes, offset);
    double value = 0.0;
    static_assert(sizeof(value) == sizeof(bits), "binary64 size mismatch");
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool allZero(const QByteArray &bytes)
{
    for (const char value : bytes) {
        if (value != 0) return false;
    }
    return true;
}

bool controlAuthorityStateContractValid(
    const ControlAuthorityStateV2 &state)
{
    const bool modeValid =
        (state.appliedMode == kUsbControlModeAutonomousV2
         || state.appliedMode == kUsbControlModeHostManagedV2)
        && (state.requestedMode == kUsbControlModeAutonomousV2
            || state.requestedMode == kUsbControlModeHostManagedV2);
    if (!modeValid || state.generation == 0U) return false;
    switch (state.phase) {
    case kUsbControlPhaseSafeStoppedV2:
        return state.owner == kUsbControlOwnerNoneV2
            && !state.hardwareActive && state.transactionId == 0U
            && state.hostLeaseDeadlineNs == 0U;
    case kUsbControlPhaseAutonomousBootstrapV2:
        return state.appliedMode == kUsbControlModeAutonomousV2
            && state.requestedMode == state.appliedMode
            && state.owner == kUsbControlOwnerBootstrapV2
            && state.hardwareActive && state.transactionId == 0U
            && state.hostLeaseDeadlineNs == 0U;
    case kUsbControlPhaseAutonomousTrackingV2:
        return state.appliedMode == kUsbControlModeAutonomousV2
            && state.requestedMode == state.appliedMode
            && state.owner == kUsbControlOwnerM509V2
            && state.hardwareActive && state.transactionId == 0U
            && state.hostLeaseDeadlineNs == 0U;
    case kUsbControlPhaseAutonomousRearmV2:
        return state.appliedMode == kUsbControlModeAutonomousV2
            && state.requestedMode == state.appliedMode
            && state.owner == kUsbControlOwnerNoneV2
            && state.hardwareActive && state.transactionId == 0U
            && state.hostLeaseDeadlineNs == 0U;
    case kUsbControlPhaseTransitionStoppingV2:
        return state.owner == kUsbControlOwnerNoneV2
            && state.transactionId != 0U
            && state.hostLeaseDeadlineNs == 0U;
    case kUsbControlPhaseTransitionReconfiguringV2:
        return state.owner == kUsbControlOwnerNoneV2
            && !state.hardwareActive && state.transactionId != 0U
            && state.hostLeaseDeadlineNs == 0U;
    case kUsbControlPhaseHostActiveV2:
        return state.appliedMode == kUsbControlModeHostManagedV2
            && state.requestedMode == state.appliedMode
            && state.owner == kUsbControlOwnerHostV2
            && state.transactionId == 0U
            && state.hostLeaseDeadlineNs != 0U;
    case kUsbControlPhaseHostSafeWaitV2:
        return state.appliedMode == kUsbControlModeHostManagedV2
            && state.requestedMode == state.appliedMode
            && state.owner == kUsbControlOwnerNoneV2
            && !state.hardwareActive && state.transactionId == 0U
            && state.hostLeaseDeadlineNs == 0U;
    default:
        return false;
    }
}

bool runtimeSinkStateValid(quint32 state)
{
    return state <= kUsbRuntimeSinkUnavailableV2;
}

bool runtimeStatusContractValid(const RuntimeStatusV2 &status)
{
    const bool lastCaptureAvailable = status.lastCaptureSequence != 0U
        && status.lastFrameCounter != 0U
        && status.lastCaptureMonotonicNs != 0U;
    const bool lastCanonicalAvailable =
        status.canonicalPhase != kUsbRuntimeCanonicalNoneV2
        && status.lastCanonicalMonotonicNs != 0U;
    const bool formalValid =
        (status.flags & kUsbRuntimeStatusFormalValidV2) != 0U;

    if ((status.flags & kUsbRuntimeStatusDaemonRunningV2) == 0U
        || status.daemonInstanceId == 0U
        || status.heartbeatSequence == 0U
        || status.startedMonotonicNs == 0U
        || status.publishedMonotonicNs < status.startedMonotonicNs
        || status.activeConfigGeneration == 0U
        || status.activeSessionId == 0U
        || status.runState < kUsbRuntimeStartingV2
        || status.runState > kUsbRuntimeStoppingV2
        || (status.controlMode != kUsbControlModeAutonomousV2
            && status.controlMode != kUsbControlModeHostManagedV2)
        || status.controlPhase < kUsbControlPhaseSafeStoppedV2
        || status.controlPhase > kUsbControlPhaseHostSafeWaitV2
        || status.controlOwner > kUsbControlOwnerHostV2
        || status.canonicalPhase > kUsbRuntimeCanonicalR3CompleteV2
        || status.templateState > kUsbRuntimeTemplateRearmingV2
        || status.lastFaultDomain > kUsbRuntimeFaultInternalV2
        || !runtimeSinkStateValid(status.formalSinkState)
        || !runtimeSinkStateValid(status.displaySinkState)
        || !runtimeSinkStateValid(status.usbTelemetrySinkState)
        || !runtimeSinkStateValid(status.usbWaveformSinkState)
        || (status.measurementValidMask & ~0x0FU) != 0U
        || (status.formalRodValidMask & ~0x0FU) != 0U
        || status.maximumAgeMs != kUsbRuntimeStatusMaximumAgeMsV2
        || status.buildIdentityCrc32 == 0U
        || status.activeContextCrc32 == 0U) {
        return false;
    }

    if (lastCaptureAvailable !=
            ((status.flags & kUsbRuntimeStatusLastCaptureAvailableV2) != 0U)
        || (!lastCaptureAvailable
            && (status.lastCaptureSequence != 0U
                || status.lastFrameCounter != 0U
                || status.lastCaptureMonotonicNs != 0U))
        || (lastCaptureAvailable
            && (status.lastCaptureMonotonicNs < status.startedMonotonicNs
                || status.lastCaptureMonotonicNs
                    > status.publishedMonotonicNs))) {
        return false;
    }
    if (lastCanonicalAvailable !=
            ((status.flags & kUsbRuntimeStatusLastCanonicalAvailableV2) != 0U)
        || (!lastCanonicalAvailable
            && status.lastCanonicalMonotonicNs != 0U)
        || (lastCanonicalAvailable
            && (status.lastCanonicalMonotonicNs < status.startedMonotonicNs
                || status.lastCanonicalMonotonicNs
                    > status.publishedMonotonicNs))) {
        return false;
    }

    if (((status.flags & kUsbRuntimeStatusHardwareActiveV2) != 0U)
            != status.hardwareActive
        || ((status.flags & kUsbRuntimeStatusTemplateActiveV2) != 0U)
            != (status.templateState == kUsbRuntimeTemplateActiveV2)
        || ((status.flags & kUsbRuntimeStatusSafeWaitV2) != 0U)
            != (status.runState == kUsbRuntimeSafeConfigurationWaitV2)
        || ((status.flags & kUsbRuntimeStatusReconfiguringV2) != 0U)
            != (status.runState == kUsbRuntimeReconfiguringV2)
        || ((status.flags & kUsbRuntimeStatusFormalSinkDegradedV2) != 0U)
            != (status.formalSinkState == kUsbRuntimeSinkUnavailableV2)
        || ((status.flags & kUsbRuntimeStatusDisplaySinkDegradedV2) != 0U)
            != (status.displaySinkState == kUsbRuntimeSinkUnavailableV2)
        || ((status.flags & kUsbRuntimeStatusTelemetrySinkDegradedV2) != 0U)
            != (status.usbTelemetrySinkState == kUsbRuntimeSinkUnavailableV2)
        || ((status.flags & kUsbRuntimeStatusWaveformSinkDegradedV2) != 0U)
            != (status.usbWaveformSinkState == kUsbRuntimeSinkUnavailableV2)) {
        return false;
    }

    if (((status.flags & kUsbRuntimeStatusCaptureLoopActiveV2) != 0U)
            != (status.runState == kUsbRuntimeActiveV2)
        || (status.runState == kUsbRuntimeActiveV2
            && !status.hardwareActive)
        || (status.runState == kUsbRuntimeSafeConfigurationWaitV2
            && status.hardwareActive)) {
        return false;
    }

    if (formalValid) {
        if (status.canonicalPhase != kUsbRuntimeCanonicalR3CompleteV2
            || status.templateState != kUsbRuntimeTemplateActiveV2
            || status.formalReason != 0U
            || status.formalRodValidMask != 0x0FU) {
            return false;
        }
    } else if (status.canonicalPhase != kUsbRuntimeCanonicalNoneV2
               && status.formalReason == 0U) {
        return false;
    }
    if (status.canonicalPhase == kUsbRuntimeCanonicalNoneV2
        && (status.formalReason != 0U
            || status.measurementValidMask != 0U
            || status.formalRodValidMask != 0U)) {
        return false;
    }
    return (status.lastFaultDomain == kUsbRuntimeFaultNoneV2)
        == (status.lastFaultCode == 0);
}

bool readFixedString(const QByteArray &bytes, int offset, int length,
                     QString *value)
{
    const QByteArray field = bytes.mid(offset, length);
    const int terminator = field.indexOf('\0');
    if (value == nullptr || terminator <= 0
        || !allZero(field.mid(terminator + 1))) {
        return false;
    }
    *value = QString::fromLatin1(field.constData(), terminator);
    return !value->isEmpty();
}

bool descriptorValid(const UsbParameterDescriptorV2 &parameter)
{
    const bool kindValid = parameter.valueKind >= 1U
        && parameter.valueKind <= 5U;
    const bool scopeValid = parameter.scope >= 1U && parameter.scope <= 3U;
    const bool accessValid = (parameter.accessFlags & ~kAccessFlagsAll) == 0U
        && (parameter.accessFlags & kUsbParameterAccessReadV2) != 0U
        && ((parameter.accessFlags & kUsbParameterAccessWriteActiveV2) == 0U
            || (parameter.accessFlags
                & kUsbParameterAccessWriteSupportedV2) != 0U);
    const bool constraintValid =
        (parameter.constraintFlags & ~kConstraintFlagsAll) == 0U;
    const bool numbersValid = std::isfinite(parameter.minimumValue)
        && std::isfinite(parameter.maximumValue)
        && std::isfinite(parameter.stepValue);
    const bool rangeValid =
        (parameter.constraintFlags & kUsbParameterConstraintRangeV2) == 0U
        || parameter.minimumValue <= parameter.maximumValue;
    const bool stepValid =
        (parameter.constraintFlags & kUsbParameterConstraintStepV2) == 0U
        || parameter.stepValue > 0.0;
    const bool enumValid =
        (parameter.constraintFlags & kUsbParameterConstraintEnumMaskV2) == 0U
        || parameter.enumMask != 0U;
    return parameter.fieldId != 0U && parameter.groupId >= 1U
        && parameter.groupId <= 11U && kindValid && scopeValid
        && accessValid && constraintValid && numbersValid
        && rangeValid && stepValid && enumValid;
}

QString number(double value)
{
    return QString::number(value, 'g', 12);
}

} // namespace

bool usbHostManagedStoppedV2(const ControlAuthorityStateV2 &state)
{
    return state.available &&
        state.appliedMode == kUsbControlModeHostManagedV2 &&
        state.requestedMode == kUsbControlModeHostManagedV2 &&
        state.phase == kUsbControlPhaseHostActiveV2 &&
        state.owner == kUsbControlOwnerHostV2 &&
        !state.hardwareActive && state.transactionId == 0U &&
        state.publishedMonotonicNs != 0U &&
        state.hostLeaseDeadlineNs > state.publishedMonotonicNs;
}

QByteArray encodeUsbParameterCatalogRequestV2(
    quint32 startIndex, quint32 maximumEntries,
    quint32 expectedCatalogCrc32, QString *error)
{
    if (maximumEntries == 0U
        || maximumEntries > kUsbParameterCatalogMaximumEntriesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 参数目录请求数量越界。");
        }
        return {};
    }
    QByteArray result;
    result.reserve(16);
    appendU32(result, startIndex);
    appendU32(result, maximumEntries);
    appendU32(result, expectedCatalogCrc32);
    appendU32(result, 0U);
    return result;
}

bool decodeUsbExtendedCapabilitiesV2(
    const QByteArray &payload, UsbExtendedCapabilitiesV2 *capabilities,
    QString *error)
{
    if (capabilities == nullptr
        || payload.size() != kUsbExtendedCapabilitiesBytesV2
        || readU32(payload, 0) != kUsbExtendedCapabilitiesTokenV2
        || readU16(payload, 4) != kUsbExtendedSchemaV2
        || readU16(payload, 6) != kUsbExtendedCapabilitiesBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 能力对象头或长度无效。");
        }
        return false;
    }

    UsbExtendedCapabilitiesV2 decoded;
    decoded.schemaVersion = readU16(payload, 4);
    decoded.flags = readU32(payload, 8);
    decoded.protocolRevision = readU32(payload, 12);
    decoded.maximumPayloadBytes = readU32(payload, 16);
    decoded.recommendedChunkBytes = readU32(payload, 20);
    decoded.maximumOutstandingRequests = readU32(payload, 24);
    decoded.configStructBytes = readU32(payload, 28);
    decoded.configReceiptBytes = readU32(payload, 32);
    decoded.parameterCatalogEntries = readU32(payload, 36);
    decoded.supportedConfigGroupMask = readU64(payload, 40);
    decoded.activeConfigGroupMask = readU64(payload, 48);
    decoded.supportedUpgradeTargetMask = readU64(payload, 56);
    decoded.activeUpgradeTargetMask = readU64(payload, 64);
    decoded.supportedFeatureMask = readU64(payload, 72);
    decoded.activeFeatureMask = readU64(payload, 80);
    decoded.afeScopeFlags = readU32(payload, 88);
    decoded.parameterCatalogCrc32 = readU32(payload, 92);
    decoded.authorityStateBytes = readU32(payload, 160);
    decoded.appliedControlMode = readU32(payload, 164);
    decoded.controlPhase = readU32(payload, 168);
    decoded.authorityGeneration = readU64(payload, 176);

    const bool stringsValid = readFixedString(
        payload, 96, 32, &decoded.deviceClass)
        && readFixedString(payload, 128, 32, &decoded.buildId);
    // Revision 9 reuses the 256-byte envelope, replacing the old reserved tail.
    // Validate the complete ARM product contract before exposing any capabilities.
    if (decoded.protocolRevision == kUsbProductProtocolRevision) {
        decoded.deviceModelControlAbi = readU32(payload, 184);
        decoded.deviceModelRequestHeaderBytes = readU32(payload, 188);
        decoded.deviceModelStateBytes = readU32(payload, 192);
        decoded.deviceModelDocumentHeaderBytes = readU32(payload, 196);
        decoded.deviceModelJsonMaxBytes = readU32(payload, 200);
        decoded.deviceModelOperationMask = readU32(payload, 204);
        decoded.resultSnapshotAbi = readU32(payload, 208);
        decoded.resultSnapshotBytes = readU32(payload, 212);
        decoded.logCatalogAbi = readU32(payload, 216);
        decoded.logEntryBytes = readU32(payload, 220);
        decoded.logEntriesMax = readU32(payload, 224);
        decoded.logChunkMax = readU32(payload, 228);
        decoded.systemInputPolicyControlAbi = readU32(payload, 232);
        decoded.systemInputPolicyRequestHeaderBytes = readU32(payload, 236);
        decoded.systemInputPolicyStateBytes = readU32(payload, 240);
        decoded.systemInputPolicyDocumentHeaderBytes = readU32(payload, 244);
        decoded.systemInputPolicyJsonMaxBytes = readU32(payload, 248);
        decoded.systemInputPolicyOperationMask = readU32(payload, 252);
        constexpr quint64 supported = 0x7FFFULL;
        constexpr quint64 requiredActive =
            kUsbExtendedFeatureCapabilitiesV2
            | kUsbExtendedFeatureControlAuthorityV2;
        const bool staging = (decoded.activeFeatureMask
            & kUsbExtendedFeatureUpgradeStagingV2) != 0U;
        const bool activation = (decoded.activeFeatureMask
            & kUsbExtendedFeatureUpgradeActivationV2) != 0U;
        const bool deviceModel = (decoded.activeFeatureMask
            & kUsbExtendedFeatureDeviceModelControlV2) != 0U;
        const bool result = (decoded.activeFeatureMask
            & kUsbExtendedFeatureResultSnapshotV1) != 0U;
        const bool logs = (decoded.activeFeatureMask
            & kUsbExtendedFeatureLogCatalogV2) != 0U;
        const bool inputPolicy = (decoded.activeFeatureMask
            & kUsbExtendedFeatureSystemInputPolicyControlV1) != 0U;
        const bool catalog = (decoded.activeFeatureMask
            & kUsbExtendedFeatureParameterCatalogV2) != 0U;
        const bool configSupported = (decoded.supportedFeatureMask
            & kUsbExtendedFeatureConfigV2) != 0U;
        const bool configActive = (decoded.activeFeatureMask
            & kUsbExtendedFeatureConfigV2) != 0U;
        const bool deviceModelAbi = deviceModel
            ? decoded.deviceModelControlAbi == 2U
                && decoded.deviceModelRequestHeaderBytes == 256U
                && decoded.deviceModelStateBytes == 352U
                && decoded.deviceModelDocumentHeaderBytes == 256U
                && decoded.deviceModelJsonMaxBytes == 16384U
                && (decoded.deviceModelOperationMask & ~63U) == 0U
                && (decoded.deviceModelOperationMask & 0x38U) == 0x38U
            : allZero(payload.mid(184, 24));
        const bool resultAbi = result
            ? decoded.resultSnapshotAbi == 3U
                && decoded.resultSnapshotBytes == 256U
            : allZero(payload.mid(208, 8));
        const bool logAbi = logs
            ? decoded.logCatalogAbi == 2U
                && decoded.logEntryBytes == 96U
                && decoded.logEntriesMax == 32U
                && decoded.logChunkMax == 65536U
            : allZero(payload.mid(216, 16));
        const bool inputPolicyAbi = inputPolicy
            ? decoded.systemInputPolicyControlAbi == 1U
                && decoded.systemInputPolicyRequestHeaderBytes == 256U
                && decoded.systemInputPolicyStateBytes == 352U
                && decoded.systemInputPolicyDocumentHeaderBytes == 256U
                && decoded.systemInputPolicyJsonMaxBytes == 4096U
                && (decoded.systemInputPolicyOperationMask & ~63U) == 0U
                && (decoded.systemInputPolicyOperationMask & 0x38U) == 0x38U
            : allZero(payload.mid(232, 24));
        const bool valid = stringsValid
            && decoded.flags == kExtendedFlagsAll
            && decoded.maximumPayloadBytes == 262144U
            && decoded.recommendedChunkBytes == 65536U
            && decoded.maximumOutstandingRequests == 1U
            && (configSupported
                ? decoded.configStructBytes
                        == static_cast<quint32>(kUsbConfigObjectBytesV2)
                    && decoded.configReceiptBytes
                        == static_cast<quint32>(kUsbConfigReceiptBytesV2)
                : decoded.configStructBytes == 0U
                    && decoded.configReceiptBytes == 0U)
            && (catalog
                ? decoded.parameterCatalogEntries > 0U
                    && decoded.parameterCatalogEntries
                        <= kUsbParameterCatalogMaximumTotalEntriesV2
                    && decoded.parameterCatalogCrc32 != 0U
                : decoded.parameterCatalogEntries == 0U
                    && decoded.parameterCatalogCrc32 == 0U)
            && (decoded.supportedConfigGroupMask & ~0x7FFULL) == 0U
            && (decoded.activeConfigGroupMask
                & ~decoded.supportedConfigGroupMask) == 0U
            && (decoded.supportedUpgradeTargetMask & ~1ULL) == 0U
            && (decoded.activeUpgradeTargetMask
                & ~decoded.supportedUpgradeTargetMask) == 0U
            && (decoded.supportedFeatureMask & ~supported) == 0U
            && (decoded.supportedFeatureMask & requiredActive) == requiredActive
            && (decoded.activeFeatureMask & requiredActive) == requiredActive
            && (decoded.activeFeatureMask
                & ~decoded.supportedFeatureMask) == 0U
            && staging == (decoded.activeUpgradeTargetMask == 1U)
            && (!activation || staging)
            && (!configSupported || catalog)
            && (!configActive || catalog)
            && (configSupported
                ? configActive && catalog
                    && decoded.supportedConfigGroupMask != 0U
                    && decoded.afeScopeFlags == 0x3FU
                : decoded.supportedConfigGroupMask == 0U
                    && decoded.activeConfigGroupMask == 0U
                    && decoded.afeScopeFlags == 0U)
            && decoded.authorityStateBytes == 128U
            && (decoded.appliedControlMode == kUsbControlModeAutonomousV2
                || decoded.appliedControlMode == kUsbControlModeHostManagedV2)
            && decoded.controlPhase >= 1U && decoded.controlPhase <= 8U
            && readU32(payload, 172) == 0U
            && decoded.authorityGeneration != 0U
            && deviceModelAbi && resultAbi && logAbi && inputPolicyAbi
            ;
        if (!valid) {
            if (error != nullptr) {
                *error = QStringLiteral("USB revision 9 能力、活动状态、ABI尺寸或保留字段不符合 ARM 产品合同。");
            }
            return false;
        }
        *capabilities = decoded;
        return true;
    }

    if (error != nullptr) {
        *error = QStringLiteral(
            "本产品只支持USB revision 9；旧revision 2兼容路径已移除。");
    }
    return false;
}

bool decodeUsbParameterCatalogChunkV2(
    const QByteArray &payload, quint32 expectedStartIndex,
    quint32 expectedCatalogCrc32, quint32 *totalEntries,
    bool *more, QVector<UsbParameterDescriptorV2> *entries,
    QString *error)
{
    if (totalEntries == nullptr || more == nullptr || entries == nullptr
        || payload.size() < kUsbParameterCatalogHeaderBytesV2
        || readU32(payload, 0) != kUsbParameterCatalogTokenV2
        || readU16(payload, 4) != kUsbExtendedSchemaV2
        || readU16(payload, 6) != kUsbParameterCatalogHeaderBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 参数目录头或长度无效。");
        }
        return false;
    }
    const quint32 catalogCrc = readU32(payload, 8);
    const quint32 total = readU32(payload, 12);
    const quint32 start = readU32(payload, 16);
    const quint32 count = readU32(payload, 20);
    const quint32 flags = readU32(payload, 24);
    const quint32 reserved = readU32(payload, 28);
    const quint64 end = static_cast<quint64>(start) + count;
    if (catalogCrc == 0U
        || (expectedCatalogCrc32 != 0U
            && catalogCrc != expectedCatalogCrc32)
        || total == 0U || total > kUsbParameterCatalogMaximumTotalEntriesV2
        || start != expectedStartIndex || end > total
        || count > kUsbParameterCatalogMaximumEntriesV2
        || (flags & ~kCatalogMore) != 0U || reserved != 0U
        || ((flags & kCatalogMore) != 0U) != (end < total)
        || payload.size() != kUsbParameterCatalogHeaderBytesV2
            + static_cast<int>(count) * kUsbParameterDescriptorBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 参数目录分页、CRC或保留位无效。");
        }
        return false;
    }
    if (start == 0U && count == total
        && usbCrc32V1(payload.mid(kUsbParameterCatalogHeaderBytesV2))
            != catalogCrc) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 完整参数目录内容CRC无效。");
        }
        return false;
    }

    QVector<UsbParameterDescriptorV2> decoded;
    decoded.reserve(static_cast<int>(count));
    for (quint32 index = 0; index < count; ++index) {
        const int offset = kUsbParameterCatalogHeaderBytesV2
            + static_cast<int>(index) * kUsbParameterDescriptorBytesV2;
        UsbParameterDescriptorV2 parameter;
        parameter.fieldId = readU32(payload, offset);
        parameter.groupId = readU16(payload, offset + 4);
        parameter.valueKind = static_cast<quint8>(payload.at(offset + 6));
        parameter.scope = static_cast<quint8>(payload.at(offset + 7));
        parameter.accessFlags = readU32(payload, offset + 8);
        parameter.constraintFlags = readU32(payload, offset + 12);
        parameter.minimumValue = readF64(payload, offset + 16);
        parameter.maximumValue = readF64(payload, offset + 24);
        parameter.stepValue = readF64(payload, offset + 32);
        parameter.enumMask = readU64(payload, offset + 40);
        if (!descriptorValid(parameter)) {
            if (error != nullptr) {
                *error = QStringLiteral("USB V2 参数目录第 %1 项无效。")
                    .arg(index);
            }
            return false;
        }
        for (const UsbParameterDescriptorV2 &existing : decoded) {
            if (existing.fieldId == parameter.fieldId) {
                if (error != nullptr) {
                    *error = QStringLiteral("USB V2 参数目录包含重复 field_id=%1。")
                        .arg(parameter.fieldId);
                }
                return false;
            }
        }
        decoded.push_back(parameter);
    }
    *totalEntries = total;
    *more = (flags & kCatalogMore) != 0U;
    *entries = decoded;
    return true;
}

QByteArray encodeUsbControlAuthorityRequestV2(
    quint32 action, quint32 requestedMode, quint64 transactionId,
    quint64 expectedGeneration, QString *error)
{
    const bool switchValid = action == kUsbAuthorityActionSwitchModeV2
        && (requestedMode == kUsbControlModeAutonomousV2
            || requestedMode == kUsbControlModeHostManagedV2);
    const bool maintenanceValid =
        (action == kUsbAuthorityActionRenewLeaseV2
         || action == kUsbAuthorityActionResumeHostV2)
        && requestedMode == 0U;
    if ((!switchValid && !maintenanceValid) || transactionId == 0U
        || expectedGeneration == 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权请求参数无效。");
        }
        return {};
    }

    QByteArray payload;
    payload.reserve(kUsbAuthorityRequestBytesV2);
    appendU32(payload, kUsbAuthorityRequestTokenV2);
    appendU16(payload, kUsbExtendedSchemaV2);
    appendU16(payload, kUsbAuthorityRequestBytesV2);
    appendU32(payload, 0U);
    appendU32(payload, action);
    appendU32(payload, requestedMode);
    appendU32(payload, 0U);
    appendU64(payload, transactionId);
    appendU64(payload, expectedGeneration);
    payload.append(QByteArray(24, '\0'));
    if (payload.size() != kUsbAuthorityRequestBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权请求内部长度错误。");
        }
        return {};
    }
    const quint32 crc = qToLittleEndian(usbCrc32V1(payload));
    memcpy(payload.data() + 8, &crc, sizeof(crc));
    return payload;
}

bool decodeUsbControlAuthorityStateV2(
    const QByteArray &payload, ControlAuthorityStateV2 *state,
    QString *error)
{
    if (state == nullptr || payload.size() != kUsbAuthorityStateBytesV2
        || readU32(payload, 0) != kUsbAuthorityStateTokenV2
        || readU16(payload, 4) != kUsbExtendedSchemaV2
        || readU16(payload, 6) != kUsbAuthorityStateBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权状态头或长度无效。");
        }
        return false;
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + 8, 0, 4);
    if (readU32(payload, 8) == 0U
        || readU32(payload, 8) != usbCrc32V1(crcImage)
        || !allZero(payload.mid(44, 4))
        || !allZero(payload.mid(88, 40))) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权状态CRC、填充或保留位无效。");
        }
        return false;
    }

    ControlAuthorityStateV2 decoded;
    decoded.available = true;
    decoded.appliedMode = readU32(payload, 12);
    decoded.requestedMode = readU32(payload, 16);
    decoded.phase = readU32(payload, 20);
    decoded.owner = readU32(payload, 24);
    decoded.transitionReason = readU32(payload, 28);
    const quint32 hardwareActive = readU32(payload, 32);
    decoded.lastResult = readI32(payload, 36);
    decoded.generation = readU64(payload, 48);
    decoded.transactionId = readU64(payload, 56);
    decoded.lastTransactionId = readU64(payload, 64);
    decoded.hostLeaseDeadlineNs = readU64(payload, 72);
    decoded.publishedMonotonicNs = readU64(payload, 80);
    if (hardwareActive > 1U || decoded.lastResult < 0
        || decoded.lastResult > 12 || decoded.publishedMonotonicNs == 0U
        || readU32(payload, 40) != 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权状态字段越界。");
        }
        return false;
    }
    decoded.hardwareActive = hardwareActive != 0U;
    if (!controlAuthorityStateContractValid(decoded)) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权模式、阶段与owner组合无效。");
        }
        return false;
    }
    *state = decoded;
    return true;
}

bool decodeUsbControlAuthorityReceiptV2(
    const QByteArray &payload, UsbControlAuthorityReceiptV2 *receipt,
    QString *error)
{
    if (receipt == nullptr || payload.size() != kUsbAuthorityReceiptBytesV2
        || readU32(payload, 0) != kUsbAuthorityReceiptTokenV2
        || readU16(payload, 4) != kUsbExtendedSchemaV2
        || readU16(payload, 6) != kUsbAuthorityReceiptBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2 控制权回执头或长度无效。");
        }
        return false;
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + 8, 0, 4);
    ControlAuthorityStateV2 state;
    QString stateError;
    const quint32 kind = readU32(payload, 12);
    const qint32 result = readI32(payload, 16);
    const quint64 transactionId = readU64(payload, 24);
    const quint64 expectedGeneration = readU64(payload, 32);
    if (readU32(payload, 8) == 0U
        || readU32(payload, 8) != usbCrc32V1(crcImage)
        || readU32(payload, 20) != 0U
        || !allZero(payload.mid(168, 24))
        || transactionId == 0U || expectedGeneration == 0U
        || kind < kUsbAuthorityReceiptAcceptedV2
        || kind > kUsbAuthorityReceiptRejectedV2
        || result < 0 || result > 12
        || !decodeUsbControlAuthorityStateV2(
            payload.mid(40, kUsbAuthorityStateBytesV2), &state,
            &stateError)
        || (kind == kUsbAuthorityReceiptAcceptedV2 && result != 11)
        || (kind == kUsbAuthorityReceiptAppliedV2
            && (result != 0 || state.lastTransactionId != transactionId))
        || (kind == kUsbAuthorityReceiptRejectedV2
            && (result == 0 || state.lastTransactionId != transactionId))) {
        if (error != nullptr) {
            *error = stateError.isEmpty()
                ? QStringLiteral("USB V2 控制权回执CRC或事务状态无效。")
                : stateError;
        }
        return false;
    }
    receipt->kind = kind;
    receipt->result = result;
    receipt->transactionId = transactionId;
    receipt->expectedGeneration = expectedGeneration;
    receipt->state = state;
    return true;
}

bool decodeUsbRuntimeStatusV2(
    const QByteArray &payload, RuntimeStatusV2 *status, QString *error)
{
    if (status == nullptr || payload.size() != kUsbRuntimeStatusBytesV2
        || readU32(payload, 0) != kUsbRuntimeStatusTokenV2
        || readU16(payload, 4) != kUsbExtendedSchemaV2
        || readU16(payload, 6) != kUsbRuntimeStatusBytesV2) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2运行状态头或长度无效。");
        }
        return false;
    }

    QByteArray crcImage = payload;
    memset(crcImage.data() + 8, 0, 4);
    if (readU32(payload, 8) == 0U
        || readU32(payload, 8) != usbCrc32V1(crcImage)
        || (readU32(payload, 12) & ~kUsbRuntimeStatusFlagsAllV2) != 0U
        || !allZero(payload.mid(200, 56))) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2运行状态CRC、标志或保留位无效。");
        }
        return false;
    }

    RuntimeStatusV2 decoded;
    decoded.flags = readU32(payload, 12);
    decoded.daemonInstanceId = readU64(payload, 16);
    decoded.heartbeatSequence = readU64(payload, 24);
    decoded.startedMonotonicNs = readU64(payload, 32);
    decoded.publishedMonotonicNs = readU64(payload, 40);
    decoded.activeConfigGeneration = readU64(payload, 48);
    decoded.activeSessionId = readU64(payload, 56);
    decoded.lastCaptureSequence = readU64(payload, 64);
    decoded.lastFrameCounter = readU64(payload, 72);
    decoded.lastCaptureMonotonicNs = readU64(payload, 80);
    decoded.lastCanonicalMonotonicNs = readU64(payload, 88);
    decoded.formalSinkDropCount = readU64(payload, 96);
    decoded.runtimeStatusDropCount = readU64(payload, 104);
    decoded.recoveryCount = readU64(payload, 112);
    decoded.runState = readU32(payload, 120);
    decoded.controlMode = readU32(payload, 124);
    decoded.controlPhase = readU32(payload, 128);
    decoded.controlOwner = readU32(payload, 132);
    const quint32 hardwareActive = readU32(payload, 136);
    decoded.canonicalPhase = readU32(payload, 140);
    decoded.templateState = readU32(payload, 144);
    decoded.formalReason = readU32(payload, 148);
    decoded.lastCycleStatus = readU32(payload, 152);
    decoded.lastFaultDomain = readU32(payload, 156);
    decoded.lastFaultCode = readI32(payload, 160);
    decoded.formalSinkState = readU32(payload, 164);
    decoded.displaySinkState = readU32(payload, 168);
    decoded.usbTelemetrySinkState = readU32(payload, 172);
    decoded.usbWaveformSinkState = readU32(payload, 176);
    decoded.measurementValidMask = readU32(payload, 180);
    decoded.formalRodValidMask = readU32(payload, 184);
    decoded.maximumAgeMs = readU32(payload, 188);
    decoded.buildIdentityCrc32 = readU32(payload, 192);
    decoded.activeContextCrc32 = readU32(payload, 196);
    if (hardwareActive > 1U) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2运行状态硬件活动字段越界。");
        }
        return false;
    }
    decoded.hardwareActive = hardwareActive != 0U;
    if (!runtimeStatusContractValid(decoded)) {
        if (error != nullptr) {
            *error = QStringLiteral("USB V2运行状态字段组合不符合ARM合同。");
        }
        return false;
    }
    decoded.available = true;
    *status = decoded;
    return true;
}

QString usbControlModeTextV2(quint32 mode)
{
    if (mode == kUsbControlModeAutonomousV2)
        return QStringLiteral("ARM自主模式");
    if (mode == kUsbControlModeHostManagedV2)
        return QStringLiteral("USB手动模式");
    return QStringLiteral("未知模式");
}

QString usbControlPhaseTextV2(quint32 phase)
{
    switch (phase) {
    case kUsbControlPhaseSafeStoppedV2: return QStringLiteral("安全停止");
    case kUsbControlPhaseAutonomousBootstrapV2: return QStringLiteral("自主建模");
    case kUsbControlPhaseAutonomousTrackingV2: return QStringLiteral("自主跟踪");
    case kUsbControlPhaseAutonomousRearmV2: return QStringLiteral("自主重整");
    case kUsbControlPhaseTransitionStoppingV2: return QStringLiteral("切换：停止激励");
    case kUsbControlPhaseTransitionReconfiguringV2: return QStringLiteral("切换：重启采集");
    case kUsbControlPhaseHostActiveV2: return QStringLiteral("USB控制有效");
    case kUsbControlPhaseHostSafeWaitV2: return QStringLiteral("USB租约失效·安全等待");
    default: return QStringLiteral("未知阶段");
    }
}

QString usbRuntimeRunStateTextV2(quint32 state)
{
    switch (state) {
    case kUsbRuntimeStartingV2: return QStringLiteral("启动中");
    case kUsbRuntimeActiveV2: return QStringLiteral("50 Hz采集中");
    case kUsbRuntimeSafeConfigurationWaitV2:
        return QStringLiteral("配置安全等待");
    case kUsbRuntimeReconfiguringV2: return QStringLiteral("配置重整");
    case kUsbRuntimeStoppingV2: return QStringLiteral("停止中");
    default: return QStringLiteral("未知运行状态");
    }
}

QString usbRuntimeCanonicalPhaseTextV2(quint32 phase)
{
    switch (phase) {
    case kUsbRuntimeCanonicalNoneV2: return QStringLiteral("尚无测量快照");
    case kUsbRuntimeCanonicalTemplatePendingV2:
        return QStringLiteral("无模板真实采集");
    case kUsbRuntimeCanonicalR3CompleteV2:
        return QStringLiteral("R3完整周期");
    default: return QStringLiteral("未知测量阶段");
    }
}

QString usbRuntimeTemplateStateTextV2(quint32 state)
{
    switch (state) {
    case kUsbRuntimeTemplateNoneV2: return QStringLiteral("未建立");
    case kUsbRuntimeTemplateBuildingV2: return QStringLiteral("建模中");
    case kUsbRuntimeTemplateActiveV2: return QStringLiteral("已激活");
    case kUsbRuntimeTemplateRearmingV2: return QStringLiteral("重整中");
    default: return QStringLiteral("未知模板状态");
    }
}

QString usbRuntimeFaultDomainTextV2(quint32 domain)
{
    switch (domain) {
    case kUsbRuntimeFaultNoneV2: return QStringLiteral("无故障");
    case kUsbRuntimeFaultHardwareV2: return QStringLiteral("硬件");
    case kUsbRuntimeFaultCaptureV2: return QStringLiteral("采集");
    case kUsbRuntimeFaultPipelineV2: return QStringLiteral("测量流水线");
    case kUsbRuntimeFaultFrontendV2: return QStringLiteral("前端控制");
    case kUsbRuntimeFaultConfigurationV2: return QStringLiteral("配置");
    case kUsbRuntimeFaultSinkV2: return QStringLiteral("输出端");
    case kUsbRuntimeFaultInternalV2: return QStringLiteral("内部");
    default: return QStringLiteral("未知故障域");
    }
}

QString usbParameterFieldNameV2(quint32 fieldId)
{
    switch (fieldId) {
    case 1: return QStringLiteral("拉杆总长");
    case 2: return QStringLiteral("A端测点");
    case 3: return QStringLiteral("纵波速度");
    case UsbParameterDeviceModelV2: return QStringLiteral("设备型号");
    case 15: return QStringLiteral("激励中心频率");
    case 16: return QStringLiteral("LNA增益");
    case 17: return QStringLiteral("数字增益步");
    case 18: return QStringLiteral("PGA增益");
    case 19: return QStringLiteral("VCNTL DAC码");
    case 20: return QStringLiteral("数字TGC衰减");
    case 21: return QStringLiteral("低通带宽");
    case 22: return QStringLiteral("数字高通");
    case 23: return QStringLiteral("LNA模拟高通");
    case 24: return QStringLiteral("有源端接");
    case 25: return QStringLiteral("AFE功耗模式");
    case 26: return QStringLiteral("LNA输入钳位");
    case 27: return QStringLiteral("低频噪声抑制");
    case 28: return QStringLiteral("PGA积分器");
    case 29: return QStringLiteral("LNA积分器");
    case 30: return QStringLiteral("Dither");
    case 31: return QStringLiteral("PGA钳位");
    case 32: return QStringLiteral("kmat_unified");
    case 33: return QStringLiteral("四路耦合偏置coupling_bias_ns");
    case 48: return QStringLiteral("B段长度");
    case 49: return QStringLiteral("D段长度");
    case 50: return QStringLiteral("E段长度");
    case 51: return QStringLiteral("AB等效面积");
    case 52: return QStringLiteral("AC面积");
    case 53: return QStringLiteral("AD等效面积");
    case 54: return QStringLiteral("B段角度");
    case 55: return QStringLiteral("D段角度");
    case 56: return QStringLiteral("A段偏置");
    case 57: return QStringLiteral("A段量化步长");
    case 64: return QStringLiteral("最小NCC峰值");
    case 65: return QStringLiteral("最小NCC峰比");
    case 66: return QStringLiteral("最小SNR");
    case 80: return QStringLiteral("模板确认帧数");
    case 81: return QStringLiteral("最小有效力");
    case 82: return QStringLiteral("偏载率报警阈值");
    case 83: return QStringLiteral("报警确认帧数");
    case UsbParameterBiasLowLoadGateNV2: return QStringLiteral("偏载率有效最小总力");
    case UsbParameterMeasurementHzV2: return QStringLiteral("测量刷新频率");
    case UsbParameterNominalHvVoltsV2: return QStringLiteral("标称发射电压");
    case UsbParameterTxBurstCyclesV2: return QStringLiteral("每帧Burst周期数");
    case UsbParameterCaptureWindowStartV2: return QStringLiteral("PL采集窗口起点");
    case UsbParameterAgcEnabledV2: return QStringLiteral("自动增益启用");
    case UsbParameterAgcFreezeWhenLoadedV2: return QStringLiteral("受力冻结AGC");
    case UsbParameterAgcReceiveBeforeHvV2: return QStringLiteral("接收增益优先");
    case UsbParameterAgcHvBeforeBurstV2: return QStringLiteral("电压优先于Burst");
    case UsbParameterAgcPeakLowPermilleV2: return QStringLiteral("AGC目标峰值下限");
    case UsbParameterAgcPeakHighPermilleV2: return QStringLiteral("AGC目标峰值上限");
    case UsbParameterAgcEmergencyPermilleV2: return QStringLiteral("AGC削顶紧急线");
    case UsbParameterAgcMaxDelayJitterNsV2: return QStringLiteral("AGC最大时延抖动");
    case UsbParameterAgcMinValidRateV2: return QStringLiteral("AGC最小有效率");
    case UsbParameterAgcMaxClippingRateV2: return QStringLiteral("AGC最大削顶率");
    case UsbParameterAgcConfirmFramesV2: return QStringLiteral("AGC确认帧数");
    case UsbParameterAgcSettleFramesV2: return QStringLiteral("AGC稳定等待帧数");
    case UsbParameterAgcMaxAdjustmentsV2: return QStringLiteral("AGC最大调整次数");
    case UsbParameterRuntimeLnaDbV2: return QStringLiteral("当前LNA增益");
    case UsbParameterRuntimePgaDbV2: return QStringLiteral("当前PGA增益");
    case UsbParameterRuntimeVcntlDacV2: return QStringLiteral("当前VCNTL DAC码");
    case UsbParameterRuntimeDigitalTgcDbV2: return QStringLiteral("当前数字TGC衰减");
    case UsbParameterRuntimeNominalHvVoltsV2: return QStringLiteral("当前标称电压");
    case UsbParameterRuntimeBurstCyclesV2: return QStringLiteral("当前Burst周期数");
    case UsbParameterRuntimeAgcStageV2: return QStringLiteral("当前AGC阶段");
    case UsbParameterRuntimeLoadedV2: return QStringLiteral("当前受力状态");
    case UsbParameterRuntimeMeasurementHzV2: return QStringLiteral("当前测量频率");
    case UsbParameterAgcVcntlMinimumV2: return QStringLiteral("VCNTL下界");
    case UsbParameterAgcVcntlMaximumV2: return QStringLiteral("VCNTL上界");
    case UsbParameterAgcVcntlFineStepV2: return QStringLiteral("VCNTL精调步长");
    case UsbParameterAgcVcntlMediumStepV2: return QStringLiteral("VCNTL中调步长");
    case UsbParameterAgcVcntlCoarseStepV2: return QStringLiteral("VCNTL粗调步长");
    case UsbParameterAgcPgaAllowedMaskV2: return QStringLiteral("PGA允许档位");
    case UsbParameterAgcLnaAllowedMaskV2: return QStringLiteral("LNA允许档位");
    case UsbParameterAgcHvMinimumV2: return QStringLiteral("自动电压下界");
    case UsbParameterAgcHvMaximumV2: return QStringLiteral("自动电压上界");
    case UsbParameterAgcHvStepV2: return QStringLiteral("自动电压步长");
    case UsbParameterAgcBurstMinimumV2: return QStringLiteral("自动Burst下界");
    case UsbParameterAgcBurstMaximumV2: return QStringLiteral("自动Burst上界");
    default: return QStringLiteral("未知字段 %1").arg(fieldId);
    }
}

QString usbParameterGroupNameV2(quint16 groupId)
{
    switch (groupId) {
    case 1: return QStringLiteral("声学路径");
    case 2: return QStringLiteral("AFE静态资产");
    case 3: return QStringLiteral("AFE全局增益");
    case 4: return QStringLiteral("AFE逐杆增益");
    case 5: return QStringLiteral("标定");
    case 6: return QStringLiteral("力学几何");
    case 7: return QStringLiteral("测量门限");
    case 8: return QStringLiteral("前端策略");
    case 9: return QStringLiteral("采集策略");
    case 10: return QStringLiteral("R3策略");
    case 11: return QStringLiteral("持久化");
    default: return QStringLiteral("未知分组 %1").arg(groupId);
    }
}

QString usbParameterScopeNameV2(quint8 scope)
{
    switch (scope) {
    case 1: return QStringLiteral("全局");
    case 2: return QStringLiteral("逐杆");
    case 3: return QStringLiteral("资产级");
    default: return QStringLiteral("未知");
    }
}

QString usbParameterValueKindNameV2(quint8 valueKind)
{
    switch (valueKind) {
    case 1: return QStringLiteral("U32");
    case 2: return QStringLiteral("I32");
    case 3: return QStringLiteral("F64");
    case 4: return QStringLiteral("枚举");
    case 5: return QStringLiteral("布尔");
    default: return QStringLiteral("未知");
    }
}

QString usbParameterAccessTextV2(quint32 accessFlags)
{
    if ((accessFlags & kUsbParameterAccessWriteActiveV2) != 0U) {
        return QStringLiteral("可写 · 已接线");
    }
    if ((accessFlags & kUsbParameterAccessAssetOnlyV2) != 0U) {
        return QStringLiteral("资产级 · 未激活");
    }
    if ((accessFlags & kUsbParameterAccessWriteSupportedV2) != 0U) {
        return QStringLiteral("源码支持 · 未激活");
    }
    return QStringLiteral("只读");
}

QString usbParameterConstraintTextV2(
    const UsbParameterDescriptorV2 &parameter)
{
    QStringList parts;
    if ((parameter.constraintFlags & kUsbParameterConstraintRangeV2) != 0U) {
        parts.push_back(QStringLiteral("%1–%2")
                            .arg(number(parameter.minimumValue),
                                 number(parameter.maximumValue)));
    }
    if ((parameter.constraintFlags & kUsbParameterConstraintStepV2) != 0U) {
        parts.push_back(QStringLiteral("步进 %1")
                            .arg(number(parameter.stepValue)));
    }
    if ((parameter.constraintFlags
         & kUsbParameterConstraintEnumMaskV2) != 0U) {
        parts.push_back(QStringLiteral("枚举 0x%1")
                            .arg(parameter.enumMask, 0, 16));
    }
    if ((parameter.constraintFlags & kUsbParameterConstraintAllowlistV2)
        != 0U) {
        parts.push_back(QStringLiteral("资产白名单"));
    }
    if ((parameter.constraintFlags & kUsbParameterConstraintContextV2)
        != 0U) {
        parts.push_back(QStringLiteral("需上下文校验"));
    }
    return parts.isEmpty() ? QStringLiteral("typed字段")
                           : parts.join(QStringLiteral(" · "));
}

UsbParameterWriteChannelV2 usbParameterWriteChannelV2(quint32 fieldId)
{
    if (fieldId >= UsbParameterRuntimeLnaDbV2
        && fieldId <= UsbParameterRuntimeMeasurementHzV2) {
        return UsbParameterWriteChannelV2::ReadOnlyRuntime;
    }
    switch (fieldId) {
    case UsbParameterDeviceModelV2:
    case 1U:
    case 2U:
    case 3U:
    case 48U:
    case 49U:
    case 50U:
    case 51U:
    case 52U:
    case 53U:
    case 54U:
    case 55U:
    case 56U:
    case 57U:
        return UsbParameterWriteChannelV2::DeviceModelDocument;
    default:
        return UsbParameterWriteChannelV2::RuntimeConfiguration;
    }
}

QString usbParameterWriteChannelTextV2(quint32 fieldId)
{
    switch (usbParameterWriteChannelV2(fieldId)) {
    case UsbParameterWriteChannelV2::ReadOnlyRuntime:
        return QStringLiteral("运行态只读");
    case UsbParameterWriteChannelV2::DeviceModelDocument:
        return QStringLiteral("型号文档 A/B持久化");
    case UsbParameterWriteChannelV2::RuntimeConfiguration:
        return QStringLiteral("运行配置 A/B持久化");
    }
    return QStringLiteral("未知");
}

bool usbParameterEffectivelyWritableV2(
    const UsbParameterDescriptorV2 &parameter, quint64 activeConfigGroupMask)
{
    if (usbParameterWriteChannelV2(parameter.fieldId)
            == UsbParameterWriteChannelV2::ReadOnlyRuntime
        || (parameter.accessFlags & kUsbParameterAccessWriteSupportedV2) == 0U
        || parameter.groupId == 0U || parameter.groupId > 63U) {
        return false;
    }
    return (activeConfigGroupMask & (1ULL << (parameter.groupId - 1U))) != 0U;
}

bool validateR2sWritableParameterCatalogV2(
    const QVector<UsbParameterDescriptorV2> &parameters, QString *error)
{
    if (parameters.size() != 64 && parameters.size() != 65) {
        if (error) *error = QStringLiteral("R2S revision 9参数目录必须包含64或65项。");
        return false;
    }
    QVector<quint32> canonicalOrder {
        4U, 1U, 2U, 3U, 48U, 49U, 50U, 51U, 52U, 53U, 54U, 55U,
        56U, 57U, 16U, 17U, 18U, 19U, 20U, 64U, 65U, 66U, 80U,
        81U, 82U, 83U, 84U, 96U, 97U, 98U, 112U, 116U, 117U, 118U,
        122U, 123U, 124U, 125U, 126U, 127U, 144U, 145U, 146U, 147U,
        148U, 149U, 150U, 151U, 152U, 153U, 154U, 155U, 113U, 114U,
        115U, 128U, 129U, 130U, 131U, 132U, 133U, 134U, 135U, 136U
    };
    if (parameters.size() == 65)
        canonicalOrder.push_back(UsbParameterCaptureWindowStartV2);
    for (qsizetype index = 0; index < parameters.size(); ++index) {
        if (parameters[index].fieldId != canonicalOrder[index]) {
            if (error) *error = QStringLiteral(
                "R2S revision 9参数目录wire顺序在第%1项不符合冻结合同。")
                    .arg(index);
            return false;
        }
    }
    const QVector<quint32> requiredWritable {
        UsbParameterDeviceModelV2, 1U, 2U, 3U, 48U, 49U, 50U, 51U, 52U,
        53U, 54U, 55U, 56U, 57U, 16U, 18U, 19U, 64U, 65U,
        66U, 80U, 81U, 82U, 83U,
        UsbParameterBiasLowLoadGateNV2, UsbParameterMeasurementHzV2,
        UsbParameterNominalHvVoltsV2, UsbParameterTxBurstCyclesV2,
        UsbParameterAgcEnabledV2, UsbParameterAgcPeakLowPermilleV2,
        UsbParameterAgcPeakHighPermilleV2,
        UsbParameterAgcEmergencyPermilleV2,
        UsbParameterAgcMaxDelayJitterNsV2,
        UsbParameterAgcMinValidRateV2,
        UsbParameterAgcMaxClippingRateV2,
        UsbParameterAgcConfirmFramesV2, UsbParameterAgcSettleFramesV2,
        UsbParameterAgcMaxAdjustmentsV2, UsbParameterAgcVcntlMinimumV2,
        UsbParameterAgcVcntlMaximumV2, UsbParameterAgcVcntlFineStepV2,
        UsbParameterAgcVcntlMediumStepV2, UsbParameterAgcVcntlCoarseStepV2,
        UsbParameterAgcPgaAllowedMaskV2, UsbParameterAgcLnaAllowedMaskV2,
        UsbParameterAgcHvMinimumV2, UsbParameterAgcHvMaximumV2,
        UsbParameterAgcHvStepV2, UsbParameterAgcBurstMinimumV2,
        UsbParameterAgcBurstMaximumV2
    };
    QVector<quint32> requiredWritableWithWindow = requiredWritable;
    if (parameters.size() == 65)
        requiredWritableWithWindow.push_back(UsbParameterCaptureWindowStartV2);
    const QVector<quint32> requiredFrozen {
        UsbParameterAgcFreezeWhenLoadedV2,
        UsbParameterAgcReceiveBeforeHvV2,
        UsbParameterAgcHvBeforeBurstV2
    };
    const QVector<quint32> requiredFixedDigital {17U, 20U};
    const QVector<quint32> requiredRuntime {
        UsbParameterRuntimeLnaDbV2, UsbParameterRuntimePgaDbV2,
        UsbParameterRuntimeVcntlDacV2,
        UsbParameterRuntimeDigitalTgcDbV2,
        UsbParameterRuntimeNominalHvVoltsV2,
        UsbParameterRuntimeBurstCyclesV2, UsbParameterRuntimeAgcStageV2,
        UsbParameterRuntimeLoadedV2, UsbParameterRuntimeMeasurementHzV2
    };
    const auto find = [&parameters](quint32 id) -> const UsbParameterDescriptorV2 * {
        for (const auto &parameter : parameters)
            if (parameter.fieldId == id) return &parameter;
        return nullptr;
    };
    for (const quint32 id : requiredWritableWithWindow) {
        const auto *parameter = find(id);
        if (parameter == nullptr
            || (parameter->accessFlags
                & (kUsbParameterAccessReadV2
                   | kUsbParameterAccessWriteSupportedV2))
                != (kUsbParameterAccessReadV2
                    | kUsbParameterAccessWriteSupportedV2)) {
            if (error) *error = QStringLiteral("参数目录缺少可写字段 %1。")
                .arg(id);
            return false;
        }
    }
    for (const quint32 id : requiredFrozen) {
        const auto *parameter = find(id);
        if (parameter == nullptr
            || (parameter->accessFlags & kUsbParameterAccessFrozenV2) == 0U
            || (parameter->accessFlags & kUsbParameterAccessWriteSupportedV2) != 0U) {
            if (error) *error = QStringLiteral("参数目录安全顺序字段 %1 必须只读冻结。")
                .arg(id);
            return false;
        }
    }
    for (const quint32 id : requiredFixedDigital) {
        const auto *parameter = find(id);
        if (parameter == nullptr
            || parameter->accessFlags != kUsbParameterAccessReadV2
            || parameter->minimumValue != 0.0
            || parameter->maximumValue != 0.0) {
            if (error) *error = QStringLiteral(
                "产品数字增益字段 %1 必须固定0且严格只读。").arg(id);
            return false;
        }
    }
    for (const quint32 id : requiredRuntime) {
        const auto *parameter = find(id);
        if (parameter == nullptr
            || parameter->accessFlags != kUsbParameterAccessReadV2) {
            if (error) *error = QStringLiteral("参数目录运行态字段 %1 必须严格只读。")
                .arg(id);
            return false;
        }
    }
    const auto *hv = find(UsbParameterNominalHvVoltsV2);
    const auto *burst = find(UsbParameterTxBurstCyclesV2);
    const auto *hz = find(UsbParameterMeasurementHzV2);
    const auto *peakRatio = find(65U);
    const auto *vcntl = find(19U);
    const auto *runtimeVcntl = find(UsbParameterRuntimeVcntlDacV2);
    const auto *vcntlMinimum = find(UsbParameterAgcVcntlMinimumV2);
    const auto *vcntlMaximum = find(UsbParameterAgcVcntlMaximumV2);
    const auto *captureWindow = find(UsbParameterCaptureWindowStartV2);
    if (hv == nullptr || hv->minimumValue != 50.0
        || hv->maximumValue != 250.0 || hv->stepValue != 5.0
        || burst == nullptr || burst->minimumValue != 1.0
        || burst->maximumValue != 8.0 || burst->stepValue != 1.0
        || hz == nullptr || hz->minimumValue != 50.0
        || hz->maximumValue != 50.0
        || peakRatio == nullptr || peakRatio->minimumValue != 1000000.0
        || peakRatio->maximumValue != 10000000.0
        || vcntl == nullptr
        || vcntl->minimumValue != kUsbRuntimeVcntlDacMinimumV9
        || vcntl->maximumValue != kUsbRuntimeVcntlDacMaximumV9
        || runtimeVcntl == nullptr
        || runtimeVcntl->minimumValue != kUsbRuntimeVcntlDacMinimumV9
        || runtimeVcntl->maximumValue != kUsbRuntimeVcntlDacMaximumV9
        || vcntlMinimum == nullptr
        || vcntlMinimum->minimumValue != kUsbRuntimeVcntlDacMinimumV9
        || vcntlMinimum->maximumValue != kUsbRuntimeVcntlDacMaximumV9
        || vcntlMaximum == nullptr
        || vcntlMaximum->minimumValue != kUsbRuntimeVcntlDacMinimumV9
        || vcntlMaximum->maximumValue != kUsbRuntimeVcntlDacMaximumV9
        || (parameters.size() == 65
            && (captureWindow == nullptr
                || captureWindow->minimumValue != 0.0
                || captureWindow->maximumValue != 91808.0
                || captureWindow->stepValue != 1.0))) {
        if (error) *error = QStringLiteral(
            "参数目录的50 Hz、电压、Burst、峰比或VCNTL边界不符合R2S合同。");
        return false;
    }
    return true;
}

} // namespace ucm
