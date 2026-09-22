#include "usb_composite_runtime_status_v1.h"

#include "usb_wire_v1.h"

#include <QtEndian>

#include <cstring>
#include <limits>

namespace ucm {
namespace {

constexpr int kCrcOffset = 8;
constexpr int kFlagsOffset = 12;
constexpr int kSnapshotGenerationOffset = 16;
constexpr int kSnapshotTimeOffset = 24;
constexpr int kConfigurationGenerationOffset = 32;
constexpr int kSystemShaOffset = 40;
constexpr int kConfigurationShaOffset = 72;
constexpr int kRecordsOffset = 104;
constexpr int kStatusPolicyVersionOffset = 344;
constexpr int kReservedOffset = 348;

constexpr int kRecordKindOffset = 0;
constexpr int kRecordFlagsOffset = 4;
constexpr int kRecordInstanceOffset = 8;
constexpr int kRecordPrimaryGenerationOffset = 16;
constexpr int kRecordSecondaryGenerationOffset = 24;
constexpr int kRecordHeartbeatSequenceOffset = 32;
constexpr int kRecordHeartbeatTimeOffset = 40;
constexpr int kRecordStateOffset = 48;
constexpr int kRecordFaultDomainOffset = 52;
constexpr int kRecordFaultCodeOffset = 56;
constexpr int kRecordAgeOffset = 60;
constexpr int kRecordFaultGenerationOffset = 64;
constexpr int kRecordBindingCrcOffset = 72;
constexpr int kRecordMaximumAgeOffset = 76;

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

quint64 readU64(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

void appendU64(QByteArray *bytes, quint64 value)
{
    const quint64 little = qToLittleEndian(value);
    bytes->append(reinterpret_cast<const char *>(&little), sizeof(little));
}

bool allZero(const QByteArray &bytes, int offset, int size)
{
    for (int index = 0; index < size; ++index) {
        if (bytes.at(offset + index) != '\0') return false;
    }
    return true;
}

bool validState(quint32 state)
{
    return state
        <= static_cast<quint32>(UsbCompositeRuntimeStateV1::Faulted);
}

bool validFaultDomain(quint32 domain)
{
    return domain
        <= static_cast<quint32>(UsbCompositeFaultDomainV1::Supervisor);
}

quint32 recordAgeMs(quint64 snapshotNs, quint64 heartbeatNs)
{
    const quint64 milliseconds = (snapshotNs - heartbeatNs) / 1000000ULL;
    if (milliseconds >= std::numeric_limits<quint32>::max()) {
        return std::numeric_limits<quint32>::max() - 1U;
    }
    return static_cast<quint32>(milliseconds);
}

quint32 bindingCrc(const QByteArray &payload)
{
    QByteArray bytes;
    bytes.reserve(112);
    bytes.append(payload.mid(kSystemShaOffset, 32));
    bytes.append(payload.mid(kConfigurationShaOffset, 32));
    appendU64(&bytes, readU64(payload, kConfigurationGenerationOffset));
    appendU64(&bytes, readU64(payload, kRecordsOffset
                                      + kRecordInstanceOffset));
    appendU64(&bytes, readU64(payload, kRecordsOffset
                                      + kUsbCompositeRuntimeRecordBytesV1
                                      + kRecordInstanceOffset));
    appendU64(&bytes, readU64(payload, kRecordsOffset
                                      + 2 * kUsbCompositeRuntimeRecordBytesV1
                                      + kRecordInstanceOffset));
    appendU64(&bytes, readU64(payload, kRecordsOffset
                                      + kRecordPrimaryGenerationOffset));
    appendU64(&bytes, readU64(payload, kRecordsOffset
                                      + kUsbCompositeRuntimeRecordBytesV1
                                      + kRecordPrimaryGenerationOffset));
    return usbCrc32V1(bytes);
}

bool policyMatches(const QByteArray &payload,
                   const UsbCompositeRuntimePolicyV1 *policy)
{
    if (policy == nullptr || !policy->allowlisted
        || policy->statusPolicyVersion == 0U
        || policy->configurationGeneration == 0U
        || policy->systemPackageSha256.size() != 32
        || policy->configurationSha256.size() != 32) {
        return false;
    }
    if (policy->statusPolicyVersion
            != readU32(payload, kStatusPolicyVersionOffset)
        || policy->configurationGeneration
            != readU64(payload, kConfigurationGenerationOffset)
        || policy->systemPackageSha256
            != payload.mid(kSystemShaOffset, 32)
        || policy->configurationSha256
            != payload.mid(kConfigurationShaOffset, 32)) {
        return false;
    }
    for (int index = 0; index < kUsbCompositeRuntimeRecordCountV1;
         ++index) {
        const quint32 maximumAge = policy->maximumAgeMs.at(index);
        if (maximumAge == 0U
            || maximumAge == std::numeric_limits<quint32>::max()
            || maximumAge != readU32(
                payload, kRecordsOffset
                    + index * kUsbCompositeRuntimeRecordBytesV1
                    + kRecordMaximumAgeOffset)) {
            return false;
        }
    }
    return true;
}

UsbCompositeRuntimeDecodeCodeV1 decodeRecord(
    const QByteArray &payload, int offset, quint32 expectedKind,
    quint64 snapshotNs, quint32 commonBindingCrc,
    UsbCompositeRuntimeRecordV1 *record)
{
    UsbCompositeRuntimeRecordV1 decoded;
    decoded.kind = readU32(payload, offset + kRecordKindOffset);
    decoded.flags = readU32(payload, offset + kRecordFlagsOffset);
    if (decoded.kind != expectedKind
        || decoded.kind
            > static_cast<quint32>(UsbCompositeRuntimeKindV1::Link)) {
        return UsbCompositeRuntimeDecodeCodeV1::State;
    }
    if ((decoded.flags & ~kUsbCompositeRecordFlagsAllV1) != 0U) {
        return UsbCompositeRuntimeDecodeCodeV1::Flags;
    }
    const bool present =
        (decoded.flags & kUsbCompositeRecordPresentV1) != 0U;
    const bool missing =
        (decoded.flags & kUsbCompositeRecordMissingV1) != 0U;
    if (present == missing) return UsbCompositeRuntimeDecodeCodeV1::Flags;

    decoded.instanceId = readU64(payload, offset + kRecordInstanceOffset);
    decoded.primaryGeneration = readU64(
        payload, offset + kRecordPrimaryGenerationOffset);
    decoded.secondaryGeneration = readU64(
        payload, offset + kRecordSecondaryGenerationOffset);
    decoded.heartbeatSequence = readU64(
        payload, offset + kRecordHeartbeatSequenceOffset);
    decoded.heartbeatMonotonicNs = readU64(
        payload, offset + kRecordHeartbeatTimeOffset);
    decoded.state = readU32(payload, offset + kRecordStateOffset);
    decoded.faultDomain = readU32(
        payload, offset + kRecordFaultDomainOffset);
    decoded.faultCode = static_cast<qint32>(
        readU32(payload, offset + kRecordFaultCodeOffset));
    decoded.ageMs = readU32(payload, offset + kRecordAgeOffset);
    decoded.faultGeneration = readU64(
        payload, offset + kRecordFaultGenerationOffset);
    decoded.bindingCrc32 = readU32(
        payload, offset + kRecordBindingCrcOffset);
    decoded.maximumAgeMs = readU32(
        payload, offset + kRecordMaximumAgeOffset);
    if (decoded.maximumAgeMs == 0U
        || decoded.maximumAgeMs
            == std::numeric_limits<quint32>::max()) {
        return UsbCompositeRuntimeDecodeCodeV1::Time;
    }
    if (missing) {
        if (decoded.flags != kUsbCompositeRecordMissingV1
            || decoded.instanceId != 0U
            || decoded.primaryGeneration != 0U
            || decoded.secondaryGeneration != 0U
            || decoded.heartbeatSequence != 0U
            || decoded.heartbeatMonotonicNs != 0U
            || decoded.state
                != static_cast<quint32>(
                    UsbCompositeRuntimeStateV1::Unknown)
            || decoded.faultDomain
                != static_cast<quint32>(
                    UsbCompositeFaultDomainV1::None)
            || decoded.faultCode != 0
            || decoded.ageMs != kUsbCompositeRuntimeMissingAgeMsV1
            || decoded.faultGeneration != 0U
            || decoded.bindingCrc32 != 0U) {
            return UsbCompositeRuntimeDecodeCodeV1::State;
        }
        *record = decoded;
        return UsbCompositeRuntimeDecodeCodeV1::Ok;
    }

    const quint32 freshness = decoded.flags
        & (kUsbCompositeRecordFreshV1 | kUsbCompositeRecordStaleV1);
    if (freshness != kUsbCompositeRecordFreshV1
        && freshness != kUsbCompositeRecordStaleV1) {
        return UsbCompositeRuntimeDecodeCodeV1::Flags;
    }
    if (decoded.instanceId == 0U || decoded.primaryGeneration == 0U
        || decoded.secondaryGeneration == 0U
        || decoded.heartbeatSequence == 0U
        || decoded.heartbeatMonotonicNs == 0U) {
        return UsbCompositeRuntimeDecodeCodeV1::Generation;
    }
    if (decoded.heartbeatMonotonicNs > snapshotNs) {
        return UsbCompositeRuntimeDecodeCodeV1::Time;
    }
    const quint32 expectedAge = recordAgeMs(
        snapshotNs, decoded.heartbeatMonotonicNs);
    if (decoded.ageMs != expectedAge
        || ((expectedAge <= decoded.maximumAgeMs)
            != (freshness == kUsbCompositeRecordFreshV1))) {
        return UsbCompositeRuntimeDecodeCodeV1::Time;
    }
    if (!validState(decoded.state)
        || decoded.state
            == static_cast<quint32>(UsbCompositeRuntimeStateV1::Unknown)
        || (((decoded.flags & kUsbCompositeRecordActiveV1) != 0U)
            != (decoded.state
                == static_cast<quint32>(
                    UsbCompositeRuntimeStateV1::Active)))) {
        return UsbCompositeRuntimeDecodeCodeV1::State;
    }
    if (!validFaultDomain(decoded.faultDomain)) {
        return UsbCompositeRuntimeDecodeCodeV1::Fault;
    }
    const bool faultLatched =
        (decoded.flags & kUsbCompositeRecordFaultLatchedV1) != 0U;
    if (decoded.faultDomain
        == static_cast<quint32>(UsbCompositeFaultDomainV1::None)) {
        if (decoded.faultCode != 0 || decoded.faultGeneration != 0U
            || faultLatched) {
            return UsbCompositeRuntimeDecodeCodeV1::Fault;
        }
    } else if (decoded.faultCode == 0
               || decoded.faultGeneration == 0U || !faultLatched) {
        return UsbCompositeRuntimeDecodeCodeV1::Fault;
    }
    const bool identityBound =
        (decoded.flags & kUsbCompositeRecordIdentityBoundV1) != 0U;
    if ((identityBound && decoded.bindingCrc32 != commonBindingCrc)
        || (!identityBound && decoded.bindingCrc32 != 0U)) {
        return UsbCompositeRuntimeDecodeCodeV1::Binding;
    }
    *record = decoded;
    return UsbCompositeRuntimeDecodeCodeV1::Ok;
}

quint32 derivedFlags(const UsbCompositeRuntimeStatusV1 &status)
{
    quint32 flags = kUsbCompositeSnapshotValidV1;
    if (status.freshnessPolicyApproved) {
        flags |= kUsbCompositeFreshnessPolicyApprovedV1;
    }
    const UsbCompositeRuntimeRecordV1 &acquisition = status.records.at(0);
    const UsbCompositeRuntimeRecordV1 &measurement = status.records.at(1);
    const UsbCompositeRuntimeRecordV1 &link = status.records.at(2);
    const bool configurationBound =
        (acquisition.flags & kUsbCompositeRecordIdentityBoundV1) != 0U
        && (measurement.flags & kUsbCompositeRecordIdentityBoundV1) != 0U
        && (link.flags & kUsbCompositeRecordIdentityBoundV1) != 0U
        && acquisition.secondaryGeneration
            == status.configurationGeneration
        && measurement.secondaryGeneration
            == status.configurationGeneration;
    if (configurationBound) flags |= kUsbCompositeConfigurationBoundV1;

    constexpr quint32 required = kUsbCompositeRecordPresentV1
        | kUsbCompositeRecordFreshV1 | kUsbCompositeRecordActiveV1
        | kUsbCompositeRecordIdentityBoundV1;
    const bool ready = configurationBound && status.freshnessPolicyApproved
        && (acquisition.flags & required) == required
        && (measurement.flags & required) == required
        && (link.flags & required) == required
        && (acquisition.flags & kUsbCompositeRecordFaultLatchedV1) == 0U
        && (measurement.flags & kUsbCompositeRecordFaultLatchedV1) == 0U
        && (link.flags & kUsbCompositeRecordFaultLatchedV1) == 0U
        && link.primaryGeneration == acquisition.primaryGeneration
        && link.secondaryGeneration == measurement.primaryGeneration;
    if (ready) flags |= kUsbCompositeRuntimeChainReadyV1;
    return flags;
}

QString runtimeStateText(quint32 state)
{
    switch (static_cast<UsbCompositeRuntimeStateV1>(state)) {
    case UsbCompositeRuntimeStateV1::Unknown:
        return QStringLiteral("UNKNOWN");
    case UsbCompositeRuntimeStateV1::Starting:
        return QStringLiteral("STARTING");
    case UsbCompositeRuntimeStateV1::Active:
        return QStringLiteral("ACTIVE");
    case UsbCompositeRuntimeStateV1::SafeWait:
        return QStringLiteral("SAFE_WAIT");
    case UsbCompositeRuntimeStateV1::Recovering:
        return QStringLiteral("RECOVERING");
    case UsbCompositeRuntimeStateV1::Stopping:
        return QStringLiteral("STOPPING");
    case UsbCompositeRuntimeStateV1::Stopped:
        return QStringLiteral("STOPPED");
    case UsbCompositeRuntimeStateV1::Faulted:
        return QStringLiteral("FAULTED");
    }
    return QStringLiteral("INVALID");
}

QString faultDomainText(quint32 domain)
{
    switch (static_cast<UsbCompositeFaultDomainV1>(domain)) {
    case UsbCompositeFaultDomainV1::None:
        return QStringLiteral("NONE");
    case UsbCompositeFaultDomainV1::Protocol:
        return QStringLiteral("PROTOCOL");
    case UsbCompositeFaultDomainV1::Driver:
        return QStringLiteral("DRIVER");
    case UsbCompositeFaultDomainV1::Pl:
        return QStringLiteral("PL");
    case UsbCompositeFaultDomainV1::Acquisition:
        return QStringLiteral("ACQUISITION");
    case UsbCompositeFaultDomainV1::Measurement:
        return QStringLiteral("MEASUREMENT");
    case UsbCompositeFaultDomainV1::Configuration:
        return QStringLiteral("CONFIGURATION");
    case UsbCompositeFaultDomainV1::Supervisor:
        return QStringLiteral("SUPERVISOR");
    }
    return QStringLiteral("INVALID");
}

CompoundRuntimeSubstatus substatusFromRecord(
    const UsbCompositeRuntimeRecordV1 &record, bool link)
{
    CompoundRuntimeSubstatus result;
    result.present =
        (record.flags & kUsbCompositeRecordPresentV1) != 0U;
    result.identityBindingKnown = true;
    result.identityBound =
        (record.flags & kUsbCompositeRecordIdentityBoundV1) != 0U;
    if (!result.present) return result;

    result.identityAvailable = true;
    result.identity = QString::number(record.instanceId);
    result.generationAvailable = true;
    result.generation = QStringLiteral("primary=%1; secondary=%2")
        .arg(record.primaryGeneration).arg(record.secondaryGeneration);
    result.heartbeatAvailable = true;
    result.heartbeat = QStringLiteral("sequence=%1; monotonic_ns=%2")
        .arg(record.heartbeatSequence).arg(record.heartbeatMonotonicNs);
    result.stateKnown = true;
    result.stateHealthy = record.state
        == static_cast<quint32>(UsbCompositeRuntimeStateV1::Active);
    result.stateText = runtimeStateText(record.state);
    result.faultKnown = true;
    result.faultActive =
        (record.flags & kUsbCompositeRecordFaultLatchedV1) != 0U;
    result.faultText = result.faultActive
        ? QStringLiteral("domain=%1; code=%2; generation=%3")
              .arg(faultDomainText(record.faultDomain))
              .arg(record.faultCode).arg(record.faultGeneration)
        : QStringLiteral("NONE");
    result.ageKnown = true;
    result.fresh =
        (record.flags & kUsbCompositeRecordFreshV1) != 0U;
    result.ageText = QStringLiteral("%1 ms / max %2 ms")
        .arg(record.ageMs).arg(record.maximumAgeMs);
    if (link) {
        result.connectivityKnown = true;
        result.connected = result.stateHealthy;
    }
    return result;
}

} // namespace

QString usbCompositeRuntimeDecodeCodeTextV1(
    UsbCompositeRuntimeDecodeCodeV1 code)
{
    switch (code) {
    case UsbCompositeRuntimeDecodeCodeV1::Ok:
        return QStringLiteral("OK");
    case UsbCompositeRuntimeDecodeCodeV1::Argument:
        return QStringLiteral("ARGUMENT");
    case UsbCompositeRuntimeDecodeCodeV1::Length:
        return QStringLiteral("LENGTH");
    case UsbCompositeRuntimeDecodeCodeV1::Token:
        return QStringLiteral("TOKEN");
    case UsbCompositeRuntimeDecodeCodeV1::Version:
        return QStringLiteral("VERSION");
    case UsbCompositeRuntimeDecodeCodeV1::Crc:
        return QStringLiteral("CRC");
    case UsbCompositeRuntimeDecodeCodeV1::Flags:
        return QStringLiteral("FLAGS");
    case UsbCompositeRuntimeDecodeCodeV1::Identity:
        return QStringLiteral("IDENTITY");
    case UsbCompositeRuntimeDecodeCodeV1::Generation:
        return QStringLiteral("GENERATION");
    case UsbCompositeRuntimeDecodeCodeV1::Time:
        return QStringLiteral("TIME");
    case UsbCompositeRuntimeDecodeCodeV1::State:
        return QStringLiteral("STATE");
    case UsbCompositeRuntimeDecodeCodeV1::Fault:
        return QStringLiteral("FAULT");
    case UsbCompositeRuntimeDecodeCodeV1::Reserved:
        return QStringLiteral("RESERVED");
    case UsbCompositeRuntimeDecodeCodeV1::Binding:
        return QStringLiteral("BINDING");
    }
    return QStringLiteral("INVALID_CODE");
}

UsbCompositeRuntimeDecodeCodeV1 decodeUsbCompositeRuntimeStatusV1(
    const QByteArray &payload,
    const UsbCompositeRuntimePolicyV1 *approvedPolicy,
    UsbCompositeRuntimeStatusV1 *status,
    QString *error)
{
    const auto reject = [error](UsbCompositeRuntimeDecodeCodeV1 code) {
        if (error != nullptr) {
            *error = QStringLiteral("CRS1解析失败：%1")
                .arg(usbCompositeRuntimeDecodeCodeTextV1(code));
        }
        return code;
    };
    if (status == nullptr) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Argument);
    }
    if (payload.size() != kUsbCompositeRuntimeBytesV1) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Length);
    }
    if (readU32(payload, 0) != kUsbCompositeRuntimeTokenV1) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Token);
    }
    if (readU16(payload, 4) != kUsbCompositeRuntimeSchemaV1
        || readU16(payload, 6) != kUsbCompositeRuntimeBytesV1) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Version);
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + kCrcOffset, 0, sizeof(quint32));
    if (readU32(payload, kCrcOffset) != usbCrc32V1(crcImage)) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Crc);
    }
    const quint32 wireFlags = readU32(payload, kFlagsOffset);
    if ((wireFlags & ~kUsbCompositeFlagsAllV1) != 0U
        || (wireFlags & kUsbCompositeSnapshotValidV1) == 0U) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Flags);
    }
    if (readU64(payload, kSnapshotGenerationOffset) == 0U
        || readU64(payload, kConfigurationGenerationOffset) == 0U) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Generation);
    }
    if (readU64(payload, kSnapshotTimeOffset) == 0U) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Time);
    }
    if (allZero(payload, kSystemShaOffset, 32)
        || allZero(payload, kConfigurationShaOffset, 32)) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Identity);
    }
    if (readU32(payload, kStatusPolicyVersionOffset) == 0U
        || !allZero(payload, kReservedOffset,
                    kUsbCompositeRuntimeBytesV1 - kReservedOffset)) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Reserved);
    }

    UsbCompositeRuntimeStatusV1 decoded;
    decoded.schemaVersion = readU16(payload, 4);
    decoded.crc32 = readU32(payload, kCrcOffset);
    decoded.flags = wireFlags;
    decoded.snapshotGeneration = readU64(
        payload, kSnapshotGenerationOffset);
    decoded.snapshotMonotonicNs = readU64(payload, kSnapshotTimeOffset);
    decoded.configurationGeneration = readU64(
        payload, kConfigurationGenerationOffset);
    decoded.systemPackageSha256 = payload.mid(kSystemShaOffset, 32);
    decoded.configurationSha256 = payload.mid(kConfigurationShaOffset, 32);
    decoded.statusPolicyVersion = readU32(
        payload, kStatusPolicyVersionOffset);
    decoded.freshnessPolicyApproved = policyMatches(
        payload, approvedPolicy);
    if (((wireFlags & kUsbCompositeFreshnessPolicyApprovedV1) != 0U)
        != decoded.freshnessPolicyApproved) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Flags);
    }

    const quint32 commonBindingCrc = bindingCrc(payload);
    for (int index = 0; index < kUsbCompositeRuntimeRecordCountV1;
         ++index) {
        const UsbCompositeRuntimeDecodeCodeV1 code = decodeRecord(
            payload,
            kRecordsOffset + index * kUsbCompositeRuntimeRecordBytesV1,
            static_cast<quint32>(index + 1), decoded.snapshotMonotonicNs,
            commonBindingCrc, &decoded.records.at(index));
        if (code != UsbCompositeRuntimeDecodeCodeV1::Ok) {
            return reject(code);
        }
    }

    const bool acquisitionBound =
        (decoded.records.at(0).flags
         & kUsbCompositeRecordIdentityBoundV1) != 0U;
    const bool measurementBound =
        (decoded.records.at(1).flags
         & kUsbCompositeRecordIdentityBoundV1) != 0U;
    const bool linkBound =
        (decoded.records.at(2).flags
         & kUsbCompositeRecordIdentityBoundV1) != 0U;
    if (acquisitionBound) {
        if (!measurementBound || !linkBound
            || decoded.records.at(0).secondaryGeneration
                != decoded.configurationGeneration
            || decoded.records.at(1).secondaryGeneration
                != decoded.configurationGeneration
            || decoded.records.at(2).primaryGeneration
                != decoded.records.at(0).primaryGeneration
            || decoded.records.at(2).secondaryGeneration
                != decoded.records.at(1).primaryGeneration) {
            return reject(UsbCompositeRuntimeDecodeCodeV1::Binding);
        }
    } else if (measurementBound || linkBound) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Binding);
    }
    if (decoded.flags != derivedFlags(decoded)) {
        return reject(UsbCompositeRuntimeDecodeCodeV1::Flags);
    }
    decoded.configurationBound =
        (decoded.flags & kUsbCompositeConfigurationBoundV1) != 0U;
    decoded.runtimeChainReady =
        (decoded.flags & kUsbCompositeRuntimeChainReadyV1) != 0U;
    *status = decoded;
    if (error != nullptr) error->clear();
    return UsbCompositeRuntimeDecodeCodeV1::Ok;
}

CompoundRuntimeSnapshot compoundRuntimeSnapshotFromUsbV1(
    const UsbCompositeRuntimeStatusV1 &status,
    const UsbCompositeRuntimePolicyV1 *approvedPolicy)
{
    CompoundRuntimeSnapshot snapshot;
    snapshot.authoritativeContractBound = true;
    snapshot.available = true;
    snapshot.sourceBytesValidated = true;
    snapshot.message = QStringLiteral("ARM CRS1对象通过严格合同校验。");
    snapshot.contractVersionKnown = true;
    snapshot.contractVersionSupported =
        status.schemaVersion == kUsbCompositeRuntimeSchemaV1;
    snapshot.contractVersion = QStringLiteral("CRS1/schema-%1")
        .arg(status.schemaVersion);
    snapshot.snapshotGenerationAvailable = true;
    snapshot.snapshotGeneration = QString::number(status.snapshotGeneration);
    snapshot.configurationGenerationAvailable = true;
    snapshot.configurationGeneration =
        QString::number(status.configurationGeneration);
    snapshot.systemPackageIdentityAvailable =
        status.systemPackageSha256.size() == 32;
    snapshot.systemPackageSha256 = QString::fromLatin1(
        status.systemPackageSha256.toHex());
    snapshot.configurationIdentityAvailable =
        status.configurationSha256.size() == 32;
    snapshot.configurationSha256 = QString::fromLatin1(
        status.configurationSha256.toHex());
    snapshot.configurationBindingKnown = true;
    snapshot.configurationBound = status.configurationBound;
    snapshot.runtimeChainClaimKnown = true;
    snapshot.runtimeChainClaim = status.runtimeChainReady;
    snapshot.acquisitiond = substatusFromRecord(status.records.at(0), false);
    snapshot.measurementd = substatusFromRecord(status.records.at(1), false);
    snapshot.cmLink = substatusFromRecord(status.records.at(2), true);
    snapshot.generationConsistencyKnown = true;
    snapshot.generationConsistent =
        status.records.at(0).secondaryGeneration
            == status.configurationGeneration
        && status.records.at(1).secondaryGeneration
            == status.configurationGeneration
        && status.records.at(2).primaryGeneration
            == status.records.at(0).primaryGeneration
        && status.records.at(2).secondaryGeneration
            == status.records.at(1).primaryGeneration;

    CompoundRuntimeFreshnessPolicyStatus &policy = snapshot.freshnessPolicy;
    policy.present = status.statusPolicyVersion != 0U;
    policy.versionAvailable = policy.present;
    policy.version = QStringLiteral("0x%1")
        .arg(status.statusPolicyVersion, 8, 16, QLatin1Char('0'))
        .toUpper();
    policy.approvalKnown = true;
    policy.approved = status.freshnessPolicyApproved;
    if (approvedPolicy != nullptr) {
        policy.identityAvailable = !approvedPolicy->identity.trimmed().isEmpty();
        policy.identity = approvedPolicy->identity;
        policy.configurationBindingKnown = true;
        policy.configurationBound = status.freshnessPolicyApproved
            && status.configurationBound;
        policy.configurationIdentity =
            approvedPolicy->configurationIdentity;
        policy.manifestBindingKnown = true;
        policy.manifestBound = status.freshnessPolicyApproved;
        policy.manifestIdentity = approvedPolicy->manifestIdentity;
    }
    return snapshot;
}

UsbCompositeRuntimeStatusResultV1 makeUsbCompositeRuntimeStatusResultV1(
    const QByteArray &payload,
    const UsbCompositeRuntimePolicyV1 *approvedPolicy)
{
    UsbCompositeRuntimeStatusResultV1 result;
    result.rawObject = payload;
    result.snapshot.authoritativeContractBound = true;
    result.snapshot.available = true;
    result.snapshot.contractVersionKnown = payload.size() >= 6;
    if (result.snapshot.contractVersionKnown) {
        result.snapshot.contractVersion = QStringLiteral("CRS1/schema-%1")
            .arg(readU16(payload, 4));
        result.snapshot.contractVersionSupported =
            readU16(payload, 4) == kUsbCompositeRuntimeSchemaV1;
    }
    QString error;
    result.code = decodeUsbCompositeRuntimeStatusV1(
        payload, approvedPolicy, &result.status, &error);
    if (result.code != UsbCompositeRuntimeDecodeCodeV1::Ok) {
        result.message = error;
        result.snapshot.message = error;
        return result;
    }
    result.success = true;
    result.message = QStringLiteral(
        "ARM CRS1复合运行状态已通过384-byte合同校验。");
    result.snapshot = compoundRuntimeSnapshotFromUsbV1(
        result.status, approvedPolicy);
    return result;
}

} // namespace ucm
