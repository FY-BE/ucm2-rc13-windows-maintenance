#pragma once

#include "usb_composite_runtime_status_v1.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <array>

namespace ucm {

// Candidate CSP1 layout introduced by ARM c14f7cd and rebound to the final
// schema-0x00010006 ARM source candidate e440037. It is an independent policy
// object; CRS1 bytes can never populate these claims.
constexpr quint32 kUsbCompositePolicyTokenV1 = 0x31505343U;
constexpr quint16 kUsbCompositePolicySchemaV1 = 1U;
constexpr int kUsbCompositePolicyBytesV1 = 176;
constexpr quint32 kUsbCompositePolicyProductApprovedV1 = 1U;
constexpr quint32 kUsbCompositePolicyTestOnlyV1 = 2U;
constexpr quint32 kUsbCompositePolicyFlagsAllV1 = 3U;
constexpr quint32 kUsbCompositeAcquisitionDescriptorTokenV1 = 0x314D4355U;
constexpr quint16 kUsbCompositeAcquisitionDescriptorAbiV1 = 4U;
constexpr int kUsbCompositeAcquisitionDescriptorBytesV1 = 256;
constexpr quint32 kUsbCompositeAcquisitionRegisterSchemaV1 = 0x00010006U;

// Exact programmable-logic candidates selected by system integration. These
// values are consumer-side fail-closed gates over signed manifest claims, not
// a Windows hardware probe or a second hardware/algorithm source of truth.
constexpr char kUsbCompositePlRoleProductV1[] = "PRODUCT";
constexpr char kUsbCompositePlRoleSafetyDiagnosticV1[] =
    "SAFETY_DIAGNOSTIC_NO_TX_NO_HV";
constexpr char kUsbCompositePlProfile50m14V1[] = "50m14";
constexpr char kUsbCompositePlProfile65m14V1[] = "65m14";
constexpr char kUsbCompositeProductPlCommitV1[] =
    "984b863aaa2ed961e409dd2b755bd32bec6154e6";
constexpr char kUsbCompositeProductPlTreeV1[] =
    "32806276e08db09ae2f8da6e6a147d791f1006bf";
constexpr char kUsbCompositeProductPlClosureSha256V1[] =
    "3150b0d0acbf5ebb780f617d643dbc0578c6fcd0229fd8a9be17a50cf541807a";
constexpr char kUsbCompositeProductPl50m14BitstreamSha256V1[] =
    "857cb7db1728f10ecb20777f8af34a0aaa6eebf7a4ca29b5bc9db61cfb001dae";
constexpr char kUsbCompositeProductPl65m14BitstreamSha256V1[] =
    "19c43e89a8073d7708fe45104e797248dc7871289403966bf8b8b1e1ac4f143f";
constexpr char kUsbCompositeDiagnosticPlCommitV1[] =
    "4ab5fdbe79ab90d5f75efe7309c13405277ac64a";
constexpr char kUsbCompositeDiagnosticPlTreeV1[] =
    "f5a69e16d81fe87506dead571a4be192ef773cc2";
constexpr char kUsbCompositeDiagnosticPlClosureSha256V1[] =
    "4e38b58ec75454ed1e3fc9d4d383a9b6553a011f1c2d5acdc22c0de1492b9e0b";
constexpr char kUsbCompositeDiagnosticPlBitstreamSha256V1[] =
    "4634f70c6b584acd3ec1bce15214b05e159634d5961e07f1ce515315a00f1f19";

enum class UsbCompositeRuntimeTrustCodeV1 : int {
    Ok = 0,
    NotConfigured = 1,
    Cleared = 2,
    Argument = -1,
    Length = -2,
    Token = -3,
    Version = -4,
    Crc = -5,
    Flags = -6,
    Fields = -7,
    Reserved = -8,
    ProductApproval = -9,
    SignatureUnknown = -10,
    SignatureInvalid = -11,
    FreshnessUnknown = -12,
    Stale = -13,
    ManifestHash = -14,
    ManifestClaims = -15,
    DuplicateConflict = -16,
    ProductDescriptor = -17,
    RuntimeIdentity = -18,
    ProgrammableLogicIdentity = -19
};

enum class UsbCompositeProgrammableLogicIdentityV1 : int {
    Unknown = 0,
    Product50m14 = 1,
    Product65m14 = 2,
    SafetyDiagnostic50m14NoTxNoHv = 3
};

struct UsbCompositePolicyManifestV1 {
    quint32 flags = 0;
    quint32 statusPolicyVersion = 0;
    std::array<quint32, kUsbCompositeRuntimeRecordCountV1> maximumAgeMs {};
    quint64 configurationGeneration = 0;
    quint64 manifestGeneration = 0;
    QByteArray systemPackageSha256;
    QByteArray configurationSha256;
    QByteArray manifestSha256;
};

// Claims are extracted from the independently verified system manifest by a
// caller-supplied verifier. They must exactly match CSP1 before a parser policy
// is made available.
struct UsbCompositeRuntimeManifestClaimsV1 {
    quint32 policyFlags = 0;
    quint32 statusPolicyVersion = 0;
    std::array<quint32, kUsbCompositeRuntimeRecordCountV1> maximumAgeMs {};
    quint64 configurationGeneration = 0;
    quint64 manifestGeneration = 0;
    QByteArray systemPackageSha256;
    QByteArray configurationSha256;
    QByteArray manifestSha256;
    quint32 acquisitionRegisterSchema = 0;
    QByteArray acquisitionProductDescriptorSha256;
    QString programmableLogicRole;
    QString programmableLogicProfile;
    QByteArray programmableLogicSourceCommit;
    QByteArray programmableLogicSourceTree;
    QByteArray programmableLogicSourceClosureSha256;
    QByteArray programmableLogicBitstreamSha256;
};

struct UsbCompositeAcquisitionIdentityV1 {
    quint16 descriptorAbi = 0;
    quint32 registerSchema = 0;
    quint32 internalCrc32 = 0;
    QByteArray descriptorSha256;
};

struct UsbCompositeRuntimeManifestVerificationV1 {
    bool signatureDecisionKnown = false;
    bool signatureValid = false;
    bool freshnessDecisionKnown = false;
    bool fresh = false;
    bool claimsAvailable = false;
    QString message;
    QString verifierIdentity;
    QString signatureScheme;
    QString policyIdentity;
    QString manifestIdentity;
    QString configurationIdentity;
    UsbCompositeRuntimeManifestClaimsV1 claims;
};

class UsbCompositeRuntimeManifestVerifierV1 {
public:
    virtual ~UsbCompositeRuntimeManifestVerifierV1() = default;
    virtual UsbCompositeRuntimeManifestVerificationV1 verify(
        const QByteArray &systemManifestObject,
        const QByteArray &detachedSignature,
        const QString &keyId) const = 0;
};

struct UsbCompositeRuntimeTrustInputV1 {
    QByteArray csp1PolicyObject;
    QByteArray systemManifestObject;
    QByteArray acquisitionProductDescriptorObject;
    QByteArray detachedSignature;
    QString keyId;
};

struct UsbCompositeRuntimeTrustResultV1 {
    bool success = false;
    QString message;
    UsbCompositeRuntimeTrustCodeV1 code =
        UsbCompositeRuntimeTrustCodeV1::NotConfigured;
    QJsonObject evidence;
};

UsbCompositeRuntimeTrustCodeV1 decodeUsbCompositePolicyManifestV1(
    const QByteArray &payload,
    UsbCompositePolicyManifestV1 *policy,
    QString *error);

UsbCompositeRuntimeTrustCodeV1 decodeUsbCompositeAcquisitionIdentityV1(
    const QByteArray &payload,
    UsbCompositeAcquisitionIdentityV1 *identity,
    QString *error);

QString usbCompositeRuntimeTrustCodeTextV1(
    UsbCompositeRuntimeTrustCodeV1 code);

UsbCompositeProgrammableLogicIdentityV1
classifyUsbCompositeProgrammableLogicIdentityV1(
    const UsbCompositeRuntimeManifestClaimsV1 &claims);

QString usbCompositeProgrammableLogicIdentityTextV1(
    UsbCompositeProgrammableLogicIdentityV1 identity);

class UsbCompositeRuntimeTrustSourceV1 final {
public:
    UsbCompositeRuntimeTrustSourceV1();

    UsbCompositeRuntimeTrustResultV1 install(
        const UsbCompositeRuntimeTrustInputV1 &input,
        const UsbCompositeRuntimeManifestVerifierV1 &verifier);
    void clear(const QString &reason);

    bool ready() const { return m_ready; }
    const UsbCompositeRuntimePolicyV1 *policy() const
    {
        return m_ready ? &m_policy : nullptr;
    }
    QJsonObject evidence() const { return m_evidence; }

private:
    UsbCompositeRuntimeTrustResultV1 reject(
        UsbCompositeRuntimeTrustCodeV1 code,
        const QString &message);
    void beginEvidence();

    bool m_ready = false;
    UsbCompositeRuntimePolicyV1 m_policy;
    quint64 m_highestManifestGeneration = 0;
    QByteArray m_highestPolicyObjectSha256;
    QJsonObject m_evidence;
};

} // namespace ucm
