#include "usb_extended_wire_v2.h"
#include "usb_wire_v1.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QTextStream>
#include <QtEndian>

#include <cstring>

using namespace ucm;

namespace {

int failures = 0;

void expect(bool condition, const QString &name)
{
    QTextStream(stdout) << (condition ? "PASS  " : "FAIL  ")
                        << name << '\n';
    if (!condition) ++failures;
}

void putU16(QByteArray *bytes, int offset, quint16 value)
{
    const quint16 little = qToLittleEndian(value);
    memcpy(bytes->data() + offset, &little, sizeof(little));
}

void putU32(QByteArray *bytes, int offset, quint32 value)
{
    const quint32 little = qToLittleEndian(value);
    memcpy(bytes->data() + offset, &little, sizeof(little));
}

void putU64(QByteArray *bytes, int offset, quint64 value)
{
    const quint64 little = qToLittleEndian(value);
    memcpy(bytes->data() + offset, &little, sizeof(little));
}

void putF64(QByteArray *bytes, int offset, double value)
{
    quint64 bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    putU64(bytes, offset, bits);
}

void sealCrc(QByteArray *bytes)
{
    putU32(bytes, 8, 0U);
    putU32(bytes, 8, usbCrc32V1(*bytes));
}

QByteArray validCapabilities()
{
    QByteArray payload(kUsbExtendedCapabilitiesBytesV2, '\0');
    putU32(&payload, 0, kUsbExtendedCapabilitiesTokenV2);
    putU16(&payload, 4, kUsbExtendedSchemaV2);
    putU16(&payload, 6, kUsbExtendedCapabilitiesBytesV2);
    putU32(&payload, 8, 0xFEU);
    putU32(&payload, 12, 2U);
    putU32(&payload, 16, 262144U);
    putU32(&payload, 20, kUsbUpgradeChunkMaximumBytesV2);
    putU32(&payload, 24, 1U);
    putU32(&payload, 28, 256U);
    putU32(&payload, 32, 512U);
    putU32(&payload, 36, 2U);
    putU64(&payload, 40, 0xFFU);
    putU64(&payload, 48, 0U);
    putU64(&payload, 56, 0U);
    putU64(&payload, 64, 0U);
    putU64(&payload, 72, 0x67U);
    putU64(&payload, 80, 0x67U);
    putU32(&payload, 88, 0x3FU);
    putU32(&payload, 92, 0x12345678U);
    memcpy(payload.data() + 96, "UCM-CONFIG-TELEMETRY-EXT-V2", 28U);
    memcpy(payload.data() + 128, "receiver-v2-test", 17U);
    putU32(&payload, 160, 128U);
    putU32(&payload, 164, kUsbControlModeAutonomousV2);
    putU32(&payload, 168, 3U);
    putU64(&payload, 176, 7U);
    return payload;
}

QByteArray minimalR2sCapabilities()
{
    QByteArray payload(kUsbExtendedCapabilitiesBytesV2, '\0');
    putU32(&payload, 0, kUsbExtendedCapabilitiesTokenV2);
    putU16(&payload, 4, kUsbExtendedSchemaV2);
    putU16(&payload, 6, kUsbExtendedCapabilitiesBytesV2);
    putU32(&payload, 8, 0xFEU);
    putU32(&payload, 12, 9U);
    putU32(&payload, 16, 262144U);
    putU32(&payload, 20, 65536U);
    putU32(&payload, 24, 1U);
    putU64(&payload, 72, kUsbExtendedFeatureCapabilitiesV2
                              | kUsbExtendedFeatureControlAuthorityV2);
    putU64(&payload, 80, kUsbExtendedFeatureCapabilitiesV2
                              | kUsbExtendedFeatureControlAuthorityV2);
    memcpy(payload.data() + 96, "UCM2-R2S", 8U);
    memcpy(payload.data() + 128, "r2s-test", 8U);
    putU32(&payload, 160, kUsbAuthorityStateBytesV2);
    putU32(&payload, 164, kUsbControlModeAutonomousV2);
    putU32(&payload, 168, kUsbControlPhaseAutonomousTrackingV2);
    putU64(&payload, 176, 1U);
    return payload;
}

void putDescriptor(QByteArray *payload, int offset, quint32 fieldId,
                   quint16 groupId, quint8 kind, quint8 scope,
                   quint32 access, quint32 constraints,
                   double minimum, double maximum, double step,
                   quint64 enumMask)
{
    putU32(payload, offset, fieldId);
    putU16(payload, offset + 4, groupId);
    (*payload)[offset + 6] = static_cast<char>(kind);
    (*payload)[offset + 7] = static_cast<char>(scope);
    putU32(payload, offset + 8, access);
    putU32(payload, offset + 12, constraints);
    putF64(payload, offset + 16, minimum);
    putF64(payload, offset + 24, maximum);
    putF64(payload, offset + 32, step);
    putU64(payload, offset + 40, enumMask);
}

QByteArray validCatalog()
{
    QByteArray payload(kUsbParameterCatalogHeaderBytesV2
                       + 2 * kUsbParameterDescriptorBytesV2, '\0');
    putU32(&payload, 0, kUsbParameterCatalogTokenV2);
    putU16(&payload, 4, kUsbExtendedSchemaV2);
    putU16(&payload, 6, kUsbParameterCatalogHeaderBytesV2);
    putU32(&payload, 8, 0U);
    putU32(&payload, 12, 2U);
    putU32(&payload, 16, 0U);
    putU32(&payload, 20, 2U);
    putU32(&payload, 24, 0U);
    putU32(&payload, 28, 0U);
    putDescriptor(&payload, 32, 16U, 4U, 4U, 2U,
                  kUsbParameterAccessReadV2
                      | kUsbParameterAccessWriteSupportedV2
                      | kUsbParameterAccessSessionRestartV2,
                  kUsbParameterConstraintEnumMaskV2
                      | kUsbParameterConstraintContextV2,
                  12.0, 24.0, 0.0,
                  (1ULL << 12) | (1ULL << 18) | (1ULL << 24));
    putDescriptor(&payload, 80, 20U, 3U, 1U, 1U,
                  kUsbParameterAccessReadV2
                      | kUsbParameterAccessWriteSupportedV2
                      | kUsbParameterAccessSessionRestartV2,
                  kUsbParameterConstraintRangeV2
                      | kUsbParameterConstraintStepV2,
                  0.0, 42.0, 6.0, 0U);
    putU32(&payload, 8, usbCrc32V1(
        payload.mid(kUsbParameterCatalogHeaderBytesV2)));
    return payload;
}

QByteArray validAuthorityState(bool hostActive = false)
{
    QByteArray payload(kUsbAuthorityStateBytesV2, '\0');
    putU32(&payload, 0, kUsbAuthorityStateTokenV2);
    putU16(&payload, 4, kUsbExtendedSchemaV2);
    putU16(&payload, 6, kUsbAuthorityStateBytesV2);
    putU32(&payload, 12, hostActive
        ? kUsbControlModeHostManagedV2 : kUsbControlModeAutonomousV2);
    putU32(&payload, 16, hostActive
        ? kUsbControlModeHostManagedV2 : kUsbControlModeAutonomousV2);
    putU32(&payload, 20, hostActive
        ? kUsbControlPhaseHostActiveV2
        : kUsbControlPhaseAutonomousTrackingV2);
    putU32(&payload, 24, hostActive
        ? kUsbControlOwnerHostV2 : kUsbControlOwnerM509V2);
    putU32(&payload, 28, 0U);
    putU32(&payload, 32, 1U);
    putU32(&payload, 36, 0U);
    putU64(&payload, 48, 7U);
    putU64(&payload, 56, 0U);
    putU64(&payload, 64, 0U);
    putU64(&payload, 72, hostActive ? 6000000000ULL : 0U);
    putU64(&payload, 80, 1000000000ULL);
    sealCrc(&payload);
    return payload;
}

QByteArray validAuthorityReceipt()
{
    QByteArray payload(kUsbAuthorityReceiptBytesV2, '\0');
    putU32(&payload, 0, kUsbAuthorityReceiptTokenV2);
    putU16(&payload, 4, kUsbExtendedSchemaV2);
    putU16(&payload, 6, kUsbAuthorityReceiptBytesV2);
    putU32(&payload, 12, kUsbAuthorityReceiptAcceptedV2);
    putU32(&payload, 16, 11U);
    putU64(&payload, 24, 900U);
    putU64(&payload, 32, 7U);
    const QByteArray state = validAuthorityState();
    memcpy(payload.data() + 40, state.constData(), state.size());
    sealCrc(&payload);
    return payload;
}

QByteArray validRuntimeStatus(bool safeWait = false)
{
    QByteArray payload(kUsbRuntimeStatusBytesV2, '\0');
    putU32(&payload, 0, kUsbRuntimeStatusTokenV2);
    putU16(&payload, 4, kUsbExtendedSchemaV2);
    putU16(&payload, 6, kUsbRuntimeStatusBytesV2);
    const quint32 flags = safeWait
        ? kUsbRuntimeStatusDaemonRunningV2 | kUsbRuntimeStatusSafeWaitV2
        : kUsbRuntimeStatusDaemonRunningV2
            | kUsbRuntimeStatusCaptureLoopActiveV2
            | kUsbRuntimeStatusHardwareActiveV2
            | kUsbRuntimeStatusLastCaptureAvailableV2
            | kUsbRuntimeStatusLastCanonicalAvailableV2
            | kUsbRuntimeStatusTemplateActiveV2
            | kUsbRuntimeStatusFormalValidV2;
    putU32(&payload, 12, flags);
    putU64(&payload, 16, 101U);
    putU64(&payload, 24, 7U);
    putU64(&payload, 32, 1000000000ULL);
    putU64(&payload, 40, 2000000000ULL);
    putU64(&payload, 48, 7U);
    putU64(&payload, 56, 9U);
    if (!safeWait) {
        putU64(&payload, 64, 10U);
        putU64(&payload, 72, 20U);
        putU64(&payload, 80, 1900000000ULL);
        putU64(&payload, 88, 1950000000ULL);
    }
    putU64(&payload, 96, 2U);
    putU64(&payload, 104, 3U);
    putU64(&payload, 112, safeWait ? 4U : 1U);
    putU32(&payload, 120, safeWait
        ? kUsbRuntimeSafeConfigurationWaitV2 : kUsbRuntimeActiveV2);
    putU32(&payload, 124, safeWait
        ? kUsbControlModeHostManagedV2 : kUsbControlModeAutonomousV2);
    putU32(&payload, 128, safeWait
        ? kUsbControlPhaseHostSafeWaitV2
        : kUsbControlPhaseAutonomousTrackingV2);
    putU32(&payload, 132, safeWait
        ? kUsbControlOwnerNoneV2 : kUsbControlOwnerM509V2);
    putU32(&payload, 136, safeWait ? 0U : 1U);
    putU32(&payload, 140, safeWait
        ? kUsbRuntimeCanonicalNoneV2 : kUsbRuntimeCanonicalR3CompleteV2);
    putU32(&payload, 144, safeWait
        ? kUsbRuntimeTemplateNoneV2 : kUsbRuntimeTemplateActiveV2);
    putU32(&payload, 148, 0U);
    putU32(&payload, 152, 0U);
    putU32(&payload, 156, safeWait
        ? kUsbRuntimeFaultConfigurationV2 : kUsbRuntimeFaultNoneV2);
    putU32(&payload, 160, safeWait
        ? static_cast<quint32>(-11) : 0U);
    const quint32 sinkState = safeWait
        ? kUsbRuntimeSinkUnknownV2 : kUsbRuntimeSinkAvailableV2;
    putU32(&payload, 164, sinkState);
    putU32(&payload, 168, sinkState);
    putU32(&payload, 172, sinkState);
    putU32(&payload, 176, sinkState);
    putU32(&payload, 180, safeWait ? 0U : 0x0FU);
    putU32(&payload, 184, safeWait ? 0U : 0x0FU);
    putU32(&payload, 188, kUsbRuntimeStatusMaximumAgeMsV2);
    putU32(&payload, 192, 0x11223344U);
    putU32(&payload, 196, 0x55667788U);
    sealCrc(&payload);
    return payload;
}

QByteArray productCapabilities()
{
    QByteArray payload = validCapabilities();
    putU32(&payload, 12, 9U);
    for (int offset : {28, 32, 36, 88, 92}) putU32(&payload, offset, 0U);
    putU64(&payload, 40, 0U);
    putU64(&payload, 56, 1U);
    putU64(&payload, 72, 0x1FF9ULL);
    putU64(&payload, 80, 0x1F21ULL);
    const quint32 tail[] = {
        2, 256, 352, 256, 16384, 63, 3, 256, 2, 96, 32, 65536,
        1, 256, 352, 256, 4096, 63
    };
    for (int i = 0; i < 18; ++i) putU32(&payload, 184 + 4 * i, tail[i]);
    return payload;
}

void productCapabilitiesAreStrictlyDecoded()
{
    QString error;
    UsbExtendedCapabilitiesV2 decoded;
    expect(!decodeUsbExtendedCapabilitiesV2(
               validCapabilities(), &decoded, &error)
               && error.contains(QStringLiteral("revision 9")),
        QStringLiteral("legacy revision 2 capability object is rejected"));
    QByteArray payload = productCapabilities();
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
        && decoded.protocolRevision == 9U
        && decoded.deviceModelControlAbi == 2U
        && decoded.resultSnapshotAbi == 3U
        && decoded.logEntryBytes == 96U
        && decoded.systemInputPolicyJsonMaxBytes == 4096U
        && decoded.configStructBytes == 0U,
        QStringLiteral("revision 9 product capabilities decode exact ABI tail"));
    putU64(&payload, 64, 1U);
    putU64(&payload, 80, 0x1FF9ULL);
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
        QStringLiteral("revision 9 permits active optional runtime and ARM upgrade"));
    // Every fixed ABI field is checked, including the last field at byte 252.
    for (int offset = 184; offset < 256; offset += 4) {
        if (offset == 204 || offset == 252) continue;
        payload = productCapabilities();
        payload[offset] = static_cast<char>(payload.at(offset) ^ 1);
        expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
            QStringLiteral("revision 9 rejects ABI mutation at %1").arg(offset));
    }
    payload = productCapabilities();
    putU32(&payload, 204, 0x38U);
    putU32(&payload, 252, 0x38U);
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
           && decoded.deviceModelOperationMask == 0x38U
           && decoded.systemInputPolicyOperationMask == 0x38U,
        QStringLiteral("revision 9 accepts truthful read-only configuration operations"));
    for (quint32 invalidMask : {0x30U, 0x78U}) {
        payload = productCapabilities();
        putU32(&payload, 204, invalidMask);
        expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
            QStringLiteral("revision 9 rejects invalid device-model operation mask %1")
                .arg(invalidMask));
        payload = productCapabilities();
        putU32(&payload, 252, invalidMask);
        expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
            QStringLiteral("revision 9 rejects invalid input-policy operation mask %1")
                .arg(invalidMask));
    }
    for (int offset : {8, 16, 20, 24, 28, 32, 36, 40, 48, 56,
                       64, 72, 80, 88, 92, 160, 164, 168, 172, 176}) {
        payload = productCapabilities();
        putU32(&payload, offset, 0xFFFFFFFFU);
        // Authority generation may be any nonzero value.
        if (offset == 176) putU64(&payload, offset, 0U);
        expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
            QStringLiteral("revision 9 rejects contract mutation at %1").arg(offset));
    }
    for (quint64 mask : {0x1F31ULL, 0x1F29ULL, 0x1F20ULL}) {
        payload = productCapabilities();
        putU64(&payload, 80, mask);
        expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
            QStringLiteral("revision 9 rejects inconsistent active feature %1").arg(mask));
    }
    payload = productCapabilities();
    putU32(&payload, 28, kUsbConfigObjectBytesV2);
    putU32(&payload, 32, kUsbConfigReceiptBytesV2);
    putU32(&payload, 36, 64U);
    putU64(&payload, 40, 0x7FFU);
    putU64(&payload, 48, 0U);
    putU64(&payload, 72, 0x1FFFULL);
    putU64(&payload, 80, 0x1F27ULL);
    putU32(&payload, 88, 0x3FU);
    putU32(&payload, 92, 0x12345678U);
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
           && decoded.parameterCatalogEntries == 64U
           && decoded.configStructBytes == kUsbConfigObjectBytesV2,
        QStringLiteral("revision 9 negotiates typed catalog and CFG2 revision 4 in reserved slots 17-19"));
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
           && decoded.activeConfigGroupMask == 0U,
        QStringLiteral("autonomous mode keeps CFG2 readable with all write groups inactive"));
    payload = productCapabilities();
    putU64(&payload, 72, 0x3FF9ULL);
    putU64(&payload, 80, 0x3F21ULL);
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
           && (decoded.activeFeatureMask
               & kUsbExtendedFeatureResultDiagnosticsV1) != 0U,
        QStringLiteral("revision 9 negotiates optional RESULT_DIAGNOSTICS feature bit13"));
    payload = productCapabilities();
    putU64(&payload, 72, 0x5FF9ULL);
    putU64(&payload, 80, 0x5F21ULL);
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
           && (decoded.activeFeatureMask
               & kUsbExtendedFeatureRuntimeControlV1) != 0U,
        QStringLiteral("revision 9 negotiates runtime progress/action feature bit14"));
    payload = productCapabilities();
    payload.replace(96, 32, QByteArray(32, 'x'));
    expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
        QStringLiteral("revision 9 rejects unterminated identity"));
    payload = productCapabilities();
    payload.chop(1);
    expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
        QStringLiteral("revision 9 rejects truncated envelope"));
    payload = productCapabilities();
    putU32(&payload, 12, 8U);
    expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
        QStringLiteral("unsupported revision cannot reinterpret revision 9 tail"));
}

void minimalR2sCapabilitiesAreNegotiated()
{
    QString error;
    UsbExtendedCapabilitiesV2 decoded;
    QByteArray payload = minimalR2sCapabilities();
    expect(decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error)
           && decoded.protocolRevision == 9U
           && decoded.activeFeatureMask == 0x21U
           && decoded.deviceModelControlAbi == 0U
           && decoded.resultSnapshotBytes == 0U,
        QStringLiteral("minimal R2S revision 9 capabilities negotiate only implemented features"));
    putU64(&payload, 80, 0x61U);
    expect(!decodeUsbExtendedCapabilitiesV2(payload, &decoded, &error),
        QStringLiteral("R2S active feature must also be declared supported"));
}

void catalogRequestAndDescriptorsAreStrict()
{
    QString error;
    const QByteArray request = encodeUsbParameterCatalogRequestV2(
        0U, 64U, 0x12345678U, &error);
    expect(request.size() == 16
           && qFromLittleEndian<quint32>(
               reinterpret_cast<const uchar *>(request.constData() + 4))
               == 64U,
        QStringLiteral("V2 catalog request has fixed 16-byte little-endian ABI"));
    expect(encodeUsbParameterCatalogRequestV2(0U, 65U, 0U, &error).isEmpty(),
        QStringLiteral("V2 catalog request rejects more than 64 entries"));

    QByteArray payload = validCatalog();
    const quint32 catalogCrc = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(payload.constData() + 8));
    quint32 total = 0;
    bool more = true;
    QVector<UsbParameterDescriptorV2> entries;
    expect(decodeUsbParameterCatalogChunkV2(
               payload, 0U, catalogCrc, &total, &more, &entries, &error)
           && total == 2U && !more && entries.size() == 2
           && entries[0].fieldId == 16U && entries[0].scope == 2U
           && entries[1].fieldId == 20U
           && entries[1].maximumValue == 42.0
           && entries[1].stepValue == 6.0,
        QStringLiteral("V2 catalog preserves per-rod/global scope and constraints"));
    expect(usbParameterAccessTextV2(entries[0].accessFlags)
               .contains(QStringLiteral("未激活"))
           && usbParameterFieldNameV2(32U)
               == QStringLiteral("kmat_unified"),
        QStringLiteral("frontend labels keep supported separate from active"));

    payload = validCatalog();
    putU32(&payload, 32 + 8,
           kUsbParameterAccessReadV2
               | kUsbParameterAccessWriteActiveV2);
    expect(!decodeUsbParameterCatalogChunkV2(
               payload, 0U, catalogCrc, &total, &more, &entries, &error),
        QStringLiteral("write-active without write-supported is rejected"));
    payload = validCatalog();
    putU32(&payload, 24, 1U);
    expect(!decodeUsbParameterCatalogChunkV2(
               payload, 0U, catalogCrc, &total, &more, &entries, &error),
        QStringLiteral("incorrect catalog pagination flag is rejected"));
}

void authorityObjectsAreStrictlyEncodedAndDecoded()
{
    QString error;
    const QByteArray request = encodeUsbControlAuthorityRequestV2(
        kUsbAuthorityActionSwitchModeV2,
        kUsbControlModeHostManagedV2, 900U, 7U, &error);
    QByteArray requestCrc = request;
    putU32(&requestCrc, 8, 0U);
    expect(request.size() == kUsbAuthorityRequestBytesV2
           && qFromLittleEndian<quint32>(
               reinterpret_cast<const uchar *>(request.constData()))
               == kUsbAuthorityRequestTokenV2
           && qFromLittleEndian<quint32>(
               reinterpret_cast<const uchar *>(request.constData() + 12))
               == kUsbAuthorityActionSwitchModeV2
           && qFromLittleEndian<quint64>(
               reinterpret_cast<const uchar *>(request.constData() + 24))
               == 900U
           && qFromLittleEndian<quint32>(
               reinterpret_cast<const uchar *>(request.constData() + 8))
               == usbCrc32V1(requestCrc),
        QStringLiteral("authority switch request is exact 64-byte sealed ABI"));
    expect(encodeUsbControlAuthorityRequestV2(
               kUsbAuthorityActionRenewLeaseV2,
               kUsbControlModeHostManagedV2, 901U, 7U, &error).isEmpty(),
        QStringLiteral("lease renewal rejects a nonzero requested mode"));

    ControlAuthorityStateV2 state;
    QByteArray statePayload = validAuthorityState(true);
    expect(decodeUsbControlAuthorityStateV2(
               statePayload, &state, &error)
           && state.available
           && state.appliedMode == kUsbControlModeHostManagedV2
           && state.phase == kUsbControlPhaseHostActiveV2
           && state.owner == kUsbControlOwnerHostV2
           && state.hardwareActive
           && state.generation == 7U
           && state.hostLeaseDeadlineNs == 6000000000ULL,
        QStringLiteral("host-active authority state preserves lease and owner"));
    expect(!usbHostManagedStoppedV2(state),
        QStringLiteral("running host-managed authority cannot write configuration or upgrade"));
    putU32(&statePayload, 32, 0U);
    sealCrc(&statePayload);
    expect(decodeUsbControlAuthorityStateV2(
               statePayload, &state, &error)
           && !state.hardwareActive
           && usbHostManagedStoppedV2(state),
        QStringLiteral("host-managed state may be safely stopped between commands"));
    state.hostLeaseDeadlineNs = state.publishedMonotonicNs;
    expect(!usbHostManagedStoppedV2(state),
        QStringLiteral("expired host lease cannot authorize configuration or upgrade"));
    putU32(&statePayload, 24, kUsbControlOwnerBootstrapV2);
    sealCrc(&statePayload);
    expect(!decodeUsbControlAuthorityStateV2(
               statePayload, &state, &error),
        QStringLiteral("invalid host-active owner is rejected even with valid CRC"));

    UsbControlAuthorityReceiptV2 receipt;
    QByteArray receiptPayload = validAuthorityReceipt();
    expect(decodeUsbControlAuthorityReceiptV2(
               receiptPayload, &receipt, &error)
           && receipt.kind == kUsbAuthorityReceiptAcceptedV2
           && receipt.result == 11
           && receipt.transactionId == 900U
           && receipt.expectedGeneration == 7U,
        QStringLiteral("authority ACCEPTED/PENDING receipt is strictly decoded"));
    putU32(&receiptPayload, 168, 1U);
    sealCrc(&receiptPayload);
    expect(!decodeUsbControlAuthorityReceiptV2(
               receiptPayload, &receipt, &error),
        QStringLiteral("authority receipt reserved mutation is rejected"));

    receiptPayload = validAuthorityReceipt();
    putU32(&receiptPayload, 12, kUsbAuthorityReceiptAppliedV2);
    putU32(&receiptPayload, 16, 0U);
    statePayload = validAuthorityState(true);
    putU32(&statePayload, 32, 0U);
    putU64(&statePayload, 64, 900U);
    sealCrc(&statePayload);
    memcpy(receiptPayload.data() + 40, statePayload.constData(), statePayload.size());
    sealCrc(&receiptPayload);
    expect(decodeUsbControlAuthorityReceiptV2(
               receiptPayload, &receipt, &error)
           && receipt.kind == kUsbAuthorityReceiptAppliedV2
           && receipt.result == 0 && !receipt.state.hardwareActive,
        QStringLiteral("synchronous R2S authority receipt is decoded without pending state"));
}

void runtimeStatusObjectsAreStrictlyDecoded()
{
    RuntimeStatusV2 status;
    QString error;
    QByteArray payload = validRuntimeStatus();
    expect(decodeUsbRuntimeStatusV2(payload, &status, &error)
           && status.available
           && status.runState == kUsbRuntimeActiveV2
           && status.hardwareActive
           && status.canonicalPhase == kUsbRuntimeCanonicalR3CompleteV2
           && status.templateState == kUsbRuntimeTemplateActiveV2
           && status.measurementValidMask == 0x0FU
           && status.formalRodValidMask == 0x0FU
           && status.heartbeatSequence == 7U,
        QStringLiteral("256-byte URS2 active snapshot is strictly decoded"));

    payload = validRuntimeStatus();
    payload[200] = 1;
    sealCrc(&payload);
    expect(!decodeUsbRuntimeStatusV2(payload, &status, &error),
        QStringLiteral("URS2 reserved mutation is rejected with valid CRC"));

    payload = validRuntimeStatus();
    putU32(&payload, 12,
           qFromLittleEndian<quint32>(
               reinterpret_cast<const uchar *>(payload.constData() + 12))
               & ~kUsbRuntimeStatusCaptureLoopActiveV2);
    sealCrc(&payload);
    expect(!decodeUsbRuntimeStatusV2(payload, &status, &error),
        QStringLiteral("URS2 active state without capture-loop flag is rejected"));

    payload = validRuntimeStatus(true);
    expect(decodeUsbRuntimeStatusV2(payload, &status, &error)
           && status.available
           && status.runState == kUsbRuntimeSafeConfigurationWaitV2
           && !status.hardwareActive
           && status.canonicalPhase == kUsbRuntimeCanonicalNoneV2
           && status.lastCaptureSequence == 0U
           && status.lastCanonicalMonotonicNs == 0U
           && status.lastFaultDomain == kUsbRuntimeFaultConfigurationV2
           && status.lastFaultCode == -11,
        QStringLiteral(
            "URS2 safe-wait is valid without telemetry or waveform snapshot"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    productCapabilitiesAreStrictlyDecoded();
    minimalR2sCapabilitiesAreNegotiated();
    catalogRequestAndDescriptorsAreStrict();
    authorityObjectsAreStrictlyEncodedAndDecoded();
    runtimeStatusObjectsAreStrictlyDecoded();
    return failures == 0 ? 0 : 1;
}
