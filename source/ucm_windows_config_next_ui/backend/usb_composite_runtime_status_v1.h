#pragma once

#include "compound_runtime_status_model.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <array>

namespace ucm {

constexpr quint32 kUsbCompositeRuntimeTokenV1 = 0x31535243U;
constexpr quint16 kUsbCompositeRuntimeSchemaV1 = 1U;
constexpr int kUsbCompositeRuntimeBytesV1 = 384;
constexpr int kUsbCompositeRuntimeRecordBytesV1 = 80;
constexpr int kUsbCompositeRuntimeRecordCountV1 = 3;
constexpr quint32 kUsbCompositeRuntimeMissingAgeMsV1 = 0xFFFFFFFFU;

constexpr quint32 kUsbCompositeSnapshotValidV1 = 1U << 0;
constexpr quint32 kUsbCompositeConfigurationBoundV1 = 1U << 1;
constexpr quint32 kUsbCompositeRuntimeChainReadyV1 = 1U << 2;
constexpr quint32 kUsbCompositeFreshnessPolicyApprovedV1 = 1U << 3;
constexpr quint32 kUsbCompositeFlagsAllV1 = 0x0FU;

constexpr quint32 kUsbCompositeRecordPresentV1 = 1U << 0;
constexpr quint32 kUsbCompositeRecordMissingV1 = 1U << 1;
constexpr quint32 kUsbCompositeRecordFreshV1 = 1U << 2;
constexpr quint32 kUsbCompositeRecordStaleV1 = 1U << 3;
constexpr quint32 kUsbCompositeRecordActiveV1 = 1U << 4;
constexpr quint32 kUsbCompositeRecordFaultLatchedV1 = 1U << 5;
constexpr quint32 kUsbCompositeRecordIdentityBoundV1 = 1U << 6;
constexpr quint32 kUsbCompositeRecordFlagsAllV1 = 0x7FU;

enum class UsbCompositeRuntimeDecodeCodeV1 : int {
    Ok = 0,
    Argument = -1,
    Length = -2,
    Token = -3,
    Version = -4,
    Crc = -5,
    Flags = -6,
    Identity = -7,
    Generation = -8,
    Time = -9,
    State = -10,
    Fault = -11,
    Reserved = -12,
    Binding = -13
};

enum class UsbCompositeRuntimeKindV1 : quint32 {
    Acquisitiond = 1,
    Measurementd = 2,
    Link = 3
};

enum class UsbCompositeRuntimeStateV1 : quint32 {
    Unknown = 0,
    Starting = 1,
    Active = 2,
    SafeWait = 3,
    Recovering = 4,
    Stopping = 5,
    Stopped = 6,
    Faulted = 7
};

enum class UsbCompositeFaultDomainV1 : quint32 {
    None = 0,
    Protocol = 1,
    Driver = 2,
    Pl = 3,
    Acquisition = 4,
    Measurement = 5,
    Configuration = 6,
    Supervisor = 7
};

struct UsbCompositeRuntimePolicyV1 {
    // Only a verified system-manifest loader may set allowlisted=true in a
    // product path. Tests use explicit fixture-only verifier identities.
    bool allowlisted = false;
    QString identity;
    QString manifestIdentity;
    QString configurationIdentity;
    quint32 statusPolicyVersion = 0;
    std::array<quint32, kUsbCompositeRuntimeRecordCountV1> maximumAgeMs {};
    quint64 configurationGeneration = 0;
    QByteArray systemPackageSha256;
    QByteArray configurationSha256;
};

struct UsbCompositeRuntimeRecordV1 {
    quint32 kind = 0;
    quint32 flags = 0;
    quint64 instanceId = 0;
    quint64 primaryGeneration = 0;
    quint64 secondaryGeneration = 0;
    quint64 heartbeatSequence = 0;
    quint64 heartbeatMonotonicNs = 0;
    quint32 state = 0;
    quint32 faultDomain = 0;
    qint32 faultCode = 0;
    quint32 ageMs = 0;
    quint64 faultGeneration = 0;
    quint32 bindingCrc32 = 0;
    quint32 maximumAgeMs = 0;
};

struct UsbCompositeRuntimeStatusV1 {
    quint16 schemaVersion = 0;
    quint32 crc32 = 0;
    quint32 flags = 0;
    quint64 snapshotGeneration = 0;
    quint64 snapshotMonotonicNs = 0;
    quint64 configurationGeneration = 0;
    QByteArray systemPackageSha256;
    QByteArray configurationSha256;
    std::array<UsbCompositeRuntimeRecordV1,
               kUsbCompositeRuntimeRecordCountV1> records {};
    quint32 statusPolicyVersion = 0;
    bool freshnessPolicyApproved = false;
    bool configurationBound = false;
    bool runtimeChainReady = false;
};

struct UsbCompositeRuntimeStatusResultV1 {
    bool success = false;
    QString message;
    UsbCompositeRuntimeDecodeCodeV1 code =
        UsbCompositeRuntimeDecodeCodeV1::Argument;
    UsbCompositeRuntimeStatusV1 status;
    CompoundRuntimeSnapshot snapshot;
    QByteArray rawObject;
    QJsonObject trustEvidence;
};

UsbCompositeRuntimeDecodeCodeV1 decodeUsbCompositeRuntimeStatusV1(
    const QByteArray &payload,
    const UsbCompositeRuntimePolicyV1 *approvedPolicy,
    UsbCompositeRuntimeStatusV1 *status,
    QString *error);

QString usbCompositeRuntimeDecodeCodeTextV1(
    UsbCompositeRuntimeDecodeCodeV1 code);

CompoundRuntimeSnapshot compoundRuntimeSnapshotFromUsbV1(
    const UsbCompositeRuntimeStatusV1 &status,
    const UsbCompositeRuntimePolicyV1 *approvedPolicy);

UsbCompositeRuntimeStatusResultV1 makeUsbCompositeRuntimeStatusResultV1(
    const QByteArray &payload,
    const UsbCompositeRuntimePolicyV1 *approvedPolicy);

} // namespace ucm
