#include "usb_composite_runtime_trust_v1.h"

#include "usb_wire_v1.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QtEndian>

#include <cstring>
#include <limits>

namespace ucm {
namespace {

constexpr int kCrcOffset = 8;
constexpr int kFlagsOffset = 12;
constexpr int kPolicyVersionOffset = 16;
constexpr int kMaximumAgeOffset = 20;
constexpr int kConfigurationGenerationOffset = 32;
constexpr int kManifestGenerationOffset = 40;
constexpr int kSystemShaOffset = 48;
constexpr int kConfigurationShaOffset = 80;
constexpr int kManifestShaOffset = 112;
constexpr int kReservedOffset = 144;
constexpr int kDescriptorInternalCrcOffset = 76;
constexpr int kDescriptorRegisterSchemaOffset = 216;

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

bool allZero(const QByteArray &bytes, int offset, int size)
{
    for (int index = 0; index < size; ++index) {
        if (bytes.at(offset + index) != '\0') return false;
    }
    return true;
}

bool nonzeroSha(const QByteArray &value)
{
    return value.size() == 32 && !allZero(value, 0, value.size());
}

QString shaText(const QByteArray &value)
{
    return value.size() == 32
        ? QString::fromLatin1(value.toHex()) : QString();
}

QString hexText(const QByteArray &value)
{
    return QString::fromLatin1(value.toHex());
}

QByteArray rawHex(const char *value)
{
    return QByteArray::fromHex(QByteArray(value));
}

bool claimsMatch(const UsbCompositePolicyManifestV1 &policy,
                 const UsbCompositeRuntimeManifestClaimsV1 &claims)
{
    return policy.flags == claims.policyFlags
        && policy.statusPolicyVersion == claims.statusPolicyVersion
        && policy.maximumAgeMs == claims.maximumAgeMs
        && policy.configurationGeneration
            == claims.configurationGeneration
        && policy.manifestGeneration == claims.manifestGeneration
        && policy.systemPackageSha256 == claims.systemPackageSha256
        && policy.configurationSha256 == claims.configurationSha256
        && policy.manifestSha256 == claims.manifestSha256;
}

QJsonArray ageArray(
    const std::array<quint32, kUsbCompositeRuntimeRecordCountV1> &ages)
{
    QJsonArray result;
    for (quint32 age : ages) result.append(QString::number(age));
    return result;
}

} // namespace

QString usbCompositeRuntimeTrustCodeTextV1(
    UsbCompositeRuntimeTrustCodeV1 code)
{
    switch (code) {
    case UsbCompositeRuntimeTrustCodeV1::Ok:
        return QStringLiteral("OK");
    case UsbCompositeRuntimeTrustCodeV1::NotConfigured:
        return QStringLiteral("NOT_CONFIGURED");
    case UsbCompositeRuntimeTrustCodeV1::Cleared:
        return QStringLiteral("CLEARED");
    case UsbCompositeRuntimeTrustCodeV1::Argument:
        return QStringLiteral("ARGUMENT");
    case UsbCompositeRuntimeTrustCodeV1::Length:
        return QStringLiteral("LENGTH");
    case UsbCompositeRuntimeTrustCodeV1::Token:
        return QStringLiteral("TOKEN");
    case UsbCompositeRuntimeTrustCodeV1::Version:
        return QStringLiteral("VERSION");
    case UsbCompositeRuntimeTrustCodeV1::Crc:
        return QStringLiteral("CRC");
    case UsbCompositeRuntimeTrustCodeV1::Flags:
        return QStringLiteral("FLAGS");
    case UsbCompositeRuntimeTrustCodeV1::Fields:
        return QStringLiteral("FIELDS");
    case UsbCompositeRuntimeTrustCodeV1::Reserved:
        return QStringLiteral("RESERVED");
    case UsbCompositeRuntimeTrustCodeV1::ProductApproval:
        return QStringLiteral("PRODUCT_APPROVAL");
    case UsbCompositeRuntimeTrustCodeV1::SignatureUnknown:
        return QStringLiteral("SIGNATURE_UNKNOWN");
    case UsbCompositeRuntimeTrustCodeV1::SignatureInvalid:
        return QStringLiteral("SIGNATURE_INVALID");
    case UsbCompositeRuntimeTrustCodeV1::FreshnessUnknown:
        return QStringLiteral("FRESHNESS_UNKNOWN");
    case UsbCompositeRuntimeTrustCodeV1::Stale:
        return QStringLiteral("STALE");
    case UsbCompositeRuntimeTrustCodeV1::ManifestHash:
        return QStringLiteral("MANIFEST_HASH");
    case UsbCompositeRuntimeTrustCodeV1::ManifestClaims:
        return QStringLiteral("MANIFEST_CLAIMS");
    case UsbCompositeRuntimeTrustCodeV1::DuplicateConflict:
        return QStringLiteral("DUPLICATE_CONFLICT");
    case UsbCompositeRuntimeTrustCodeV1::ProductDescriptor:
        return QStringLiteral("PRODUCT_DESCRIPTOR");
    case UsbCompositeRuntimeTrustCodeV1::RuntimeIdentity:
        return QStringLiteral("RUNTIME_IDENTITY");
    case UsbCompositeRuntimeTrustCodeV1::ProgrammableLogicIdentity:
        return QStringLiteral("PROGRAMMABLE_LOGIC_IDENTITY");
    }
    return QStringLiteral("INVALID_CODE");
}

UsbCompositeProgrammableLogicIdentityV1
classifyUsbCompositeProgrammableLogicIdentityV1(
    const UsbCompositeRuntimeManifestClaimsV1 &claims)
{
    const bool productSource =
        claims.programmableLogicRole
            == QString::fromLatin1(kUsbCompositePlRoleProductV1)
        && claims.programmableLogicSourceCommit
            == rawHex(kUsbCompositeProductPlCommitV1)
        && claims.programmableLogicSourceTree
            == rawHex(kUsbCompositeProductPlTreeV1)
        && claims.programmableLogicSourceClosureSha256
            == rawHex(kUsbCompositeProductPlClosureSha256V1);
    if (productSource
        && claims.programmableLogicProfile
            == QString::fromLatin1(kUsbCompositePlProfile50m14V1)
        && claims.programmableLogicBitstreamSha256
            == rawHex(kUsbCompositeProductPl50m14BitstreamSha256V1)) {
        return UsbCompositeProgrammableLogicIdentityV1::Product50m14;
    }
    if (productSource
        && claims.programmableLogicProfile
            == QString::fromLatin1(kUsbCompositePlProfile65m14V1)
        && claims.programmableLogicBitstreamSha256
            == rawHex(kUsbCompositeProductPl65m14BitstreamSha256V1)) {
        return UsbCompositeProgrammableLogicIdentityV1::Product65m14;
    }
    if (claims.programmableLogicRole
            == QString::fromLatin1(
                kUsbCompositePlRoleSafetyDiagnosticV1)
        && claims.programmableLogicProfile
            == QString::fromLatin1(kUsbCompositePlProfile50m14V1)
        && claims.programmableLogicSourceCommit
            == rawHex(kUsbCompositeDiagnosticPlCommitV1)
        && claims.programmableLogicSourceTree
            == rawHex(kUsbCompositeDiagnosticPlTreeV1)
        && claims.programmableLogicSourceClosureSha256
            == rawHex(kUsbCompositeDiagnosticPlClosureSha256V1)
        && claims.programmableLogicBitstreamSha256
            == rawHex(kUsbCompositeDiagnosticPlBitstreamSha256V1)) {
        return UsbCompositeProgrammableLogicIdentityV1::
            SafetyDiagnostic50m14NoTxNoHv;
    }
    return UsbCompositeProgrammableLogicIdentityV1::Unknown;
}

QString usbCompositeProgrammableLogicIdentityTextV1(
    UsbCompositeProgrammableLogicIdentityV1 identity)
{
    switch (identity) {
    case UsbCompositeProgrammableLogicIdentityV1::Unknown:
        return QStringLiteral("UNKNOWN");
    case UsbCompositeProgrammableLogicIdentityV1::Product50m14:
        return QStringLiteral("PRODUCT_50M14");
    case UsbCompositeProgrammableLogicIdentityV1::Product65m14:
        return QStringLiteral("PRODUCT_65M14");
    case UsbCompositeProgrammableLogicIdentityV1::
            SafetyDiagnostic50m14NoTxNoHv:
        return QStringLiteral("SAFETY_DIAGNOSTIC_50M14_NO_TX_NO_HV");
    }
    return QStringLiteral("UNKNOWN");
}

UsbCompositeRuntimeTrustCodeV1 decodeUsbCompositeAcquisitionIdentityV1(
    const QByteArray &payload,
    UsbCompositeAcquisitionIdentityV1 *identity,
    QString *error)
{
    const auto reject = [error](UsbCompositeRuntimeTrustCodeV1 code) {
        if (error != nullptr) {
            *error = QStringLiteral("acquisition product descriptor解析失败：%1")
                .arg(usbCompositeRuntimeTrustCodeTextV1(code));
        }
        return code;
    };
    if (identity == nullptr) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Argument);
    }
    if (payload.size() != kUsbCompositeAcquisitionDescriptorBytesV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ProductDescriptor);
    }
    if (readU32(payload, 0) != kUsbCompositeAcquisitionDescriptorTokenV1
        || readU16(payload, 4) != kUsbCompositeAcquisitionDescriptorAbiV1
        || readU16(payload, 6)
            != kUsbCompositeAcquisitionDescriptorBytesV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ProductDescriptor);
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + kDescriptorInternalCrcOffset, 0,
           sizeof(quint32));
    if (readU32(payload, kDescriptorInternalCrcOffset)
        != usbCrc32V1(crcImage)) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ProductDescriptor);
    }
    UsbCompositeAcquisitionIdentityV1 decoded;
    decoded.descriptorAbi = readU16(payload, 4);
    decoded.registerSchema = readU32(
        payload, kDescriptorRegisterSchemaOffset);
    decoded.internalCrc32 = readU32(
        payload, kDescriptorInternalCrcOffset);
    decoded.descriptorSha256 = QCryptographicHash::hash(
        payload, QCryptographicHash::Sha256);
    if (decoded.registerSchema == 0U) {
        return reject(UsbCompositeRuntimeTrustCodeV1::RuntimeIdentity);
    }
    *identity = decoded;
    if (error != nullptr) error->clear();
    return UsbCompositeRuntimeTrustCodeV1::Ok;
}

UsbCompositeRuntimeTrustCodeV1 decodeUsbCompositePolicyManifestV1(
    const QByteArray &payload,
    UsbCompositePolicyManifestV1 *policy,
    QString *error)
{
    const auto reject = [error](UsbCompositeRuntimeTrustCodeV1 code) {
        if (error != nullptr) {
            *error = QStringLiteral("CSP1解析失败：%1")
                .arg(usbCompositeRuntimeTrustCodeTextV1(code));
        }
        return code;
    };
    if (policy == nullptr) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Argument);
    }
    if (payload.size() != kUsbCompositePolicyBytesV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Length);
    }
    if (readU32(payload, 0) != kUsbCompositePolicyTokenV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Token);
    }
    if (readU16(payload, 4) != kUsbCompositePolicySchemaV1
        || readU16(payload, 6) != kUsbCompositePolicyBytesV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Version);
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + kCrcOffset, 0, sizeof(quint32));
    if (readU32(payload, kCrcOffset) != usbCrc32V1(crcImage)) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Crc);
    }

    UsbCompositePolicyManifestV1 decoded;
    decoded.flags = readU32(payload, kFlagsOffset);
    if ((decoded.flags & ~kUsbCompositePolicyFlagsAllV1) != 0U
        || (decoded.flags != kUsbCompositePolicyProductApprovedV1
            && decoded.flags != kUsbCompositePolicyTestOnlyV1)) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Flags);
    }
    decoded.statusPolicyVersion = readU32(payload, kPolicyVersionOffset);
    for (int index = 0; index < kUsbCompositeRuntimeRecordCountV1;
         ++index) {
        decoded.maximumAgeMs.at(index) = readU32(
            payload, kMaximumAgeOffset + index * 4);
    }
    decoded.configurationGeneration = readU64(
        payload, kConfigurationGenerationOffset);
    decoded.manifestGeneration = readU64(payload, kManifestGenerationOffset);
    decoded.systemPackageSha256 = payload.mid(kSystemShaOffset, 32);
    decoded.configurationSha256 = payload.mid(kConfigurationShaOffset, 32);
    decoded.manifestSha256 = payload.mid(kManifestShaOffset, 32);

    if (decoded.statusPolicyVersion == 0U
        || decoded.configurationGeneration == 0U
        || decoded.manifestGeneration == 0U
        || !nonzeroSha(decoded.systemPackageSha256)
        || !nonzeroSha(decoded.configurationSha256)
        || !nonzeroSha(decoded.manifestSha256)) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Fields);
    }
    for (quint32 maximumAge : decoded.maximumAgeMs) {
        if (maximumAge == 0U
            || maximumAge == std::numeric_limits<quint32>::max()) {
            return reject(UsbCompositeRuntimeTrustCodeV1::Fields);
        }
    }
    if (!allZero(payload, kReservedOffset,
                 kUsbCompositePolicyBytesV1 - kReservedOffset)) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Reserved);
    }
    *policy = decoded;
    if (error != nullptr) error->clear();
    return UsbCompositeRuntimeTrustCodeV1::Ok;
}

UsbCompositeRuntimeTrustSourceV1::UsbCompositeRuntimeTrustSourceV1()
{
    beginEvidence();
    m_evidence.insert(QStringLiteral("status"),
                      QStringLiteral("NOT_CONFIGURED"));
    m_evidence.insert(QStringLiteral("message"), QStringLiteral(
        "未显式注入独立的CSP1/system-manifest/product-descriptor与精确PL身份claims。"));
}

void UsbCompositeRuntimeTrustSourceV1::beginEvidence()
{
    m_evidence = {
        {QStringLiteral("schema"),
         QStringLiteral("UCM.WINDOWS.CRS1.TRUST.CANDIDATE.3")},
        {QStringLiteral("arm_candidate_commit"),
         QStringLiteral("e440037f415c1d2ae76f59227596ca9867bc45cb")},
        {QStringLiteral("arm_candidate_tree"),
         QStringLiteral("dd837f231e8b839da47feb3ed8a5b711d2c2fcbe")},
        {QStringLiteral("required_acquisition_register_schema"),
         QStringLiteral("0x00010006")},
        {QStringLiteral("explicit_injection"), true},
        {QStringLiteral("crs1_self_approval"), false},
        {QStringLiteral("product_trust_material_embedded"), false},
        {QStringLiteral("runtime_identity_schema_embedded"), true},
        {QStringLiteral("programmable_logic_candidate_gate_embedded"), true},
        {QStringLiteral("programmable_logic_identity_source"),
         QStringLiteral("SIGNED_SYSTEM_MANIFEST_CLAIMS_ONLY")},
        {QStringLiteral("direct_driver_access"), false},
        {QStringLiteral("hardware_truth_authority"), false},
        {QStringLiteral("algorithm_truth_authority"), false},
        {QStringLiteral("ready"), false},
        {QStringLiteral("highest_manifest_generation"),
         QString::number(m_highestManifestGeneration)}
    };
}

UsbCompositeRuntimeTrustResultV1
UsbCompositeRuntimeTrustSourceV1::reject(
    UsbCompositeRuntimeTrustCodeV1 code,
    const QString &message)
{
    m_ready = false;
    m_policy = {};
    m_evidence.insert(QStringLiteral("ready"), false);
    m_evidence.insert(QStringLiteral("status"),
                      usbCompositeRuntimeTrustCodeTextV1(code));
    m_evidence.insert(QStringLiteral("message"), message);
    m_evidence.insert(QStringLiteral("highest_manifest_generation"),
                      QString::number(m_highestManifestGeneration));
    return {false, message, code, m_evidence};
}

void UsbCompositeRuntimeTrustSourceV1::clear(const QString &reason)
{
    beginEvidence();
    (void)reject(UsbCompositeRuntimeTrustCodeV1::Cleared,
                 reason.trimmed().isEmpty()
                     ? QStringLiteral("CRS1本机信任状态已清除。")
                     : reason);
}

UsbCompositeRuntimeTrustResultV1
UsbCompositeRuntimeTrustSourceV1::install(
    const UsbCompositeRuntimeTrustInputV1 &input,
    const UsbCompositeRuntimeManifestVerifierV1 &verifier)
{
    m_ready = false;
    m_policy = {};
    beginEvidence();
    m_evidence.insert(QStringLiteral("csp1_bytes"),
                      input.csp1PolicyObject.size());
    m_evidence.insert(QStringLiteral("system_manifest_bytes"),
                      input.systemManifestObject.size());
    m_evidence.insert(QStringLiteral("acquisition_product_descriptor_bytes"),
                      input.acquisitionProductDescriptorObject.size());
    m_evidence.insert(QStringLiteral("signature_present"),
                      !input.detachedSignature.isEmpty());
    m_evidence.insert(QStringLiteral("key_id"), input.keyId);
    if (input.csp1PolicyObject.isEmpty()
        || input.systemManifestObject.isEmpty()
        || input.acquisitionProductDescriptorObject.isEmpty()
        || input.detachedSignature.isEmpty()
        || input.keyId.trimmed().isEmpty()) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Argument,
                      QStringLiteral(
                          "CSP1、system manifest、acquisition product descriptor、签名或key ID为空。"));
    }

    UsbCompositePolicyManifestV1 candidate;
    QString decodeError;
    const UsbCompositeRuntimeTrustCodeV1 decodeCode =
        decodeUsbCompositePolicyManifestV1(
            input.csp1PolicyObject, &candidate, &decodeError);
    if (decodeCode != UsbCompositeRuntimeTrustCodeV1::Ok) {
        return reject(decodeCode, decodeError);
    }
    const QByteArray policyObjectSha256 = QCryptographicHash::hash(
        input.csp1PolicyObject, QCryptographicHash::Sha256);
    m_evidence.insert(QStringLiteral("policy_object_sha256"),
                      shaText(policyObjectSha256));
    m_evidence.insert(QStringLiteral("policy_flags"),
                      static_cast<int>(candidate.flags));
    m_evidence.insert(QStringLiteral("status_policy_version"),
                      QString::number(candidate.statusPolicyVersion));
    m_evidence.insert(QStringLiteral("maximum_age_ms"),
                      ageArray(candidate.maximumAgeMs));
    m_evidence.insert(QStringLiteral("configuration_generation"),
                      QString::number(candidate.configurationGeneration));
    m_evidence.insert(QStringLiteral("manifest_generation"),
                      QString::number(candidate.manifestGeneration));
    m_evidence.insert(QStringLiteral("system_package_sha256"),
                      shaText(candidate.systemPackageSha256));
    m_evidence.insert(QStringLiteral("configuration_sha256"),
                      shaText(candidate.configurationSha256));
    m_evidence.insert(QStringLiteral("manifest_sha256"),
                      shaText(candidate.manifestSha256));
    if (candidate.flags != kUsbCompositePolicyProductApprovedV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ProductApproval,
                      QStringLiteral("TEST_ONLY CSP1不能激活产品CRS1信任。"));
    }

    UsbCompositeAcquisitionIdentityV1 acquisitionIdentity;
    QString identityError;
    const UsbCompositeRuntimeTrustCodeV1 identityCode =
        decodeUsbCompositeAcquisitionIdentityV1(
            input.acquisitionProductDescriptorObject,
            &acquisitionIdentity, &identityError);
    if (identityCode != UsbCompositeRuntimeTrustCodeV1::Ok) {
        return reject(identityCode, identityError);
    }
    m_evidence.insert(QStringLiteral("acquisition_register_schema"),
                      QStringLiteral("0x%1")
                          .arg(acquisitionIdentity.registerSchema,
                               8, 16, QLatin1Char('0')));
    m_evidence.insert(
        QStringLiteral("acquisition_product_descriptor_sha256"),
        shaText(acquisitionIdentity.descriptorSha256));
    if (acquisitionIdentity.registerSchema
        != kUsbCompositeAcquisitionRegisterSchemaV1) {
        return reject(UsbCompositeRuntimeTrustCodeV1::RuntimeIdentity,
                      QStringLiteral(
                          "acquisition register schema不是当前绑定的0x00010006。"));
    }

    const UsbCompositeRuntimeManifestVerificationV1 verification =
        verifier.verify(input.systemManifestObject,
                        input.detachedSignature, input.keyId);
    m_evidence.insert(QStringLiteral("verifier_identity"),
                      verification.verifierIdentity);
    m_evidence.insert(QStringLiteral("signature_scheme"),
                      verification.signatureScheme);
    m_evidence.insert(QStringLiteral("verifier_message"),
                      verification.message);
    m_evidence.insert(QStringLiteral("verified_policy_flags"),
                      static_cast<int>(verification.claims.policyFlags));
    if (!verification.signatureDecisionKnown) {
        return reject(UsbCompositeRuntimeTrustCodeV1::SignatureUnknown,
                      QStringLiteral("system manifest签名结论未知。"));
    }
    if (!verification.signatureValid) {
        return reject(UsbCompositeRuntimeTrustCodeV1::SignatureInvalid,
                      QStringLiteral("system manifest签名验证失败。"));
    }
    if (!verification.freshnessDecisionKnown) {
        return reject(UsbCompositeRuntimeTrustCodeV1::FreshnessUnknown,
                      QStringLiteral("system manifest freshness结论未知。"));
    }
    if (!verification.fresh) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Stale,
                      QStringLiteral("system manifest已陈旧。"));
    }
    if (!verification.claimsAvailable
        || verification.verifierIdentity.trimmed().isEmpty()
        || verification.signatureScheme.trimmed().isEmpty()
        || verification.policyIdentity.trimmed().isEmpty()
        || verification.manifestIdentity.trimmed().isEmpty()
        || verification.configurationIdentity.trimmed().isEmpty()) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                      QStringLiteral("签名验证器未提供完整、具名的policy claims。"));
    }
    if (verification.claims.acquisitionRegisterSchema
            != acquisitionIdentity.registerSchema
        || verification.claims.acquisitionProductDescriptorSha256
            != acquisitionIdentity.descriptorSha256) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                      QStringLiteral(
                           "acquisition product descriptor身份与签名manifest claims不一致。"));
    }

    const QByteArray manifestObjectSha256 = QCryptographicHash::hash(
        input.systemManifestObject, QCryptographicHash::Sha256);
    m_evidence.insert(QStringLiteral("manifest_object_sha256"),
                      shaText(manifestObjectSha256));
    if (manifestObjectSha256 != candidate.manifestSha256
        || manifestObjectSha256 != verification.claims.manifestSha256) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ManifestHash,
                      QStringLiteral("system manifest对象SHA-256与CSP1/验证claims不一致。"));
    }
    if (!claimsMatch(candidate, verification.claims)) {
        return reject(UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                      QStringLiteral("CSP1字段与独立签名manifest claims不一致。"));
    }

    const UsbCompositeProgrammableLogicIdentityV1 plIdentity =
        classifyUsbCompositeProgrammableLogicIdentityV1(
            verification.claims);
    const bool diagnosticIdentity =
        plIdentity == UsbCompositeProgrammableLogicIdentityV1::
            SafetyDiagnostic50m14NoTxNoHv;
    const bool productIdentity =
        plIdentity == UsbCompositeProgrammableLogicIdentityV1::Product50m14
        || plIdentity
            == UsbCompositeProgrammableLogicIdentityV1::Product65m14;
    m_evidence.insert(QStringLiteral("programmable_logic_role"),
                      verification.claims.programmableLogicRole);
    m_evidence.insert(QStringLiteral("programmable_logic_profile"),
                      verification.claims.programmableLogicProfile);
    m_evidence.insert(QStringLiteral("programmable_logic_source_commit"),
                      hexText(verification.claims.programmableLogicSourceCommit));
    m_evidence.insert(QStringLiteral("programmable_logic_source_tree"),
                      hexText(verification.claims.programmableLogicSourceTree));
    m_evidence.insert(
        QStringLiteral("programmable_logic_source_closure_sha256"),
        shaText(verification.claims.programmableLogicSourceClosureSha256));
    m_evidence.insert(
        QStringLiteral("programmable_logic_bitstream_sha256"),
        shaText(verification.claims.programmableLogicBitstreamSha256));
    m_evidence.insert(QStringLiteral("programmable_logic_identity"),
                      usbCompositeProgrammableLogicIdentityTextV1(plIdentity));
    m_evidence.insert(QStringLiteral("programmable_logic_claims_signed"), true);
    m_evidence.insert(QStringLiteral("programmable_logic_product_eligible"),
                      productIdentity);
    m_evidence.insert(QStringLiteral("programmable_logic_diagnostic_only"),
                      diagnosticIdentity);
    m_evidence.insert(QStringLiteral("programmable_logic_tx_authorized"), false);
    m_evidence.insert(QStringLiteral("programmable_logic_hv_authorized"), false);
    if (diagnosticIdentity) {
        m_evidence.insert(QStringLiteral("programmable_logic_tx_path_present"),
                          false);
        m_evidence.insert(QStringLiteral("programmable_logic_hv_path_present"),
                          false);
        return reject(UsbCompositeRuntimeTrustCodeV1::ProductApproval,
                      QStringLiteral(
                          "安全诊断PL身份仅可显示为独立NO_TX_NO_HV诊断，不能激活产品CRS1信任。"));
    }
    if (!productIdentity) {
        return reject(
            UsbCompositeRuntimeTrustCodeV1::ProgrammableLogicIdentity,
            QStringLiteral(
                "签名manifest中的PL commit/tree/source-closure/profile/bitstream不是当前精确产品候选。"));
    }
    if (candidate.manifestGeneration < m_highestManifestGeneration) {
        return reject(UsbCompositeRuntimeTrustCodeV1::Stale,
                      QStringLiteral("CSP1 manifest generation低于本进程已接受水位。"));
    }
    if (candidate.manifestGeneration == m_highestManifestGeneration
        && m_highestManifestGeneration != 0U
        && policyObjectSha256 != m_highestPolicyObjectSha256) {
        return reject(UsbCompositeRuntimeTrustCodeV1::DuplicateConflict,
                      QStringLiteral("同一manifest generation出现冲突CSP1对象。"));
    }

    UsbCompositeRuntimePolicyV1 policy;
    policy.allowlisted = true;
    policy.identity = verification.policyIdentity;
    policy.manifestIdentity = verification.manifestIdentity;
    policy.configurationIdentity = verification.configurationIdentity;
    policy.statusPolicyVersion = candidate.statusPolicyVersion;
    policy.maximumAgeMs = candidate.maximumAgeMs;
    policy.configurationGeneration = candidate.configurationGeneration;
    policy.systemPackageSha256 = candidate.systemPackageSha256;
    policy.configurationSha256 = candidate.configurationSha256;
    m_policy = policy;
    m_ready = true;
    if (candidate.manifestGeneration > m_highestManifestGeneration) {
        m_highestManifestGeneration = candidate.manifestGeneration;
        m_highestPolicyObjectSha256 = policyObjectSha256;
    }
    m_evidence.insert(QStringLiteral("ready"), true);
    m_evidence.insert(QStringLiteral("status"), QStringLiteral("OK"));
    m_evidence.insert(QStringLiteral("message"), QStringLiteral(
        "独立CSP1/system-manifest签名、freshness、ARM/PL身份、哈希和claims已闭合。"));
    m_evidence.insert(QStringLiteral("policy_identity"),
                      verification.policyIdentity);
    m_evidence.insert(QStringLiteral("manifest_identity"),
                      verification.manifestIdentity);
    m_evidence.insert(QStringLiteral("configuration_identity"),
                      verification.configurationIdentity);
    m_evidence.insert(QStringLiteral("highest_manifest_generation"),
                      QString::number(m_highestManifestGeneration));
    return {true, m_evidence.value(QStringLiteral("message")).toString(),
            UsbCompositeRuntimeTrustCodeV1::Ok, m_evidence};
}

} // namespace ucm
