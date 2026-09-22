#include "usb_composite_runtime_trust_v1.h"

#include "usb_wire_v1.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
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

void sealCrc(QByteArray *bytes)
{
    putU32(bytes, 8, 0U);
    putU32(bytes, 8, usbCrc32V1(*bytes));
}

QByteArray encodeCsp1(const UsbCompositeRuntimeManifestClaimsV1 &claims,
                      quint32 flags = kUsbCompositePolicyProductApprovedV1)
{
    QByteArray bytes(kUsbCompositePolicyBytesV1, '\0');
    putU32(&bytes, 0, kUsbCompositePolicyTokenV1);
    putU16(&bytes, 4, kUsbCompositePolicySchemaV1);
    putU16(&bytes, 6, kUsbCompositePolicyBytesV1);
    putU32(&bytes, 12, flags);
    putU32(&bytes, 16, claims.statusPolicyVersion);
    for (int index = 0; index < kUsbCompositeRuntimeRecordCountV1;
         ++index) {
        putU32(&bytes, 20 + index * 4, claims.maximumAgeMs.at(index));
    }
    putU64(&bytes, 32, claims.configurationGeneration);
    putU64(&bytes, 40, claims.manifestGeneration);
    memcpy(bytes.data() + 48, claims.systemPackageSha256.constData(), 32);
    memcpy(bytes.data() + 80, claims.configurationSha256.constData(), 32);
    memcpy(bytes.data() + 112, claims.manifestSha256.constData(), 32);
    sealCrc(&bytes);
    return bytes;
}

struct Fixture {
    UsbCompositeRuntimeTrustInputV1 input;
    UsbCompositeRuntimeManifestVerificationV1 verification;
};

QByteArray descriptorFixture(const QString &name)
{
    QFile file(QStringLiteral(UCM_D021_FIXTURE_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        expect(false, QStringLiteral("descriptor fixture opens: %1").arg(name));
        return {};
    }
    const QByteArray encoded = file.readAll();
    QByteArray compact;
    compact.reserve(encoded.size());
    for (char byte : encoded) {
        if (!QChar::fromLatin1(byte).isSpace()) compact.append(byte);
    }
    return QByteArray::fromHex(compact);
}

QJsonObject jsonFixture(const QString &name)
{
    QFile file(QStringLiteral(UCM_D021_FIXTURE_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        expect(false, QStringLiteral("JSON fixture opens: %1").arg(name));
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void setProductPlIdentity(UsbCompositeRuntimeManifestClaimsV1 *claims,
                          const char *profile,
                          const char *bitstreamSha256)
{
    claims->programmableLogicRole =
        QString::fromLatin1(kUsbCompositePlRoleProductV1);
    claims->programmableLogicProfile = QString::fromLatin1(profile);
    claims->programmableLogicSourceCommit = QByteArray::fromHex(
        QByteArray(kUsbCompositeProductPlCommitV1));
    claims->programmableLogicSourceTree = QByteArray::fromHex(
        QByteArray(kUsbCompositeProductPlTreeV1));
    claims->programmableLogicSourceClosureSha256 = QByteArray::fromHex(
        QByteArray(kUsbCompositeProductPlClosureSha256V1));
    claims->programmableLogicBitstreamSha256 = QByteArray::fromHex(
        QByteArray(bitstreamSha256));
}

void setDiagnosticPlIdentity(UsbCompositeRuntimeManifestClaimsV1 *claims)
{
    claims->programmableLogicRole = QString::fromLatin1(
        kUsbCompositePlRoleSafetyDiagnosticV1);
    claims->programmableLogicProfile = QString::fromLatin1(
        kUsbCompositePlProfile50m14V1);
    claims->programmableLogicSourceCommit = QByteArray::fromHex(
        QByteArray(kUsbCompositeDiagnosticPlCommitV1));
    claims->programmableLogicSourceTree = QByteArray::fromHex(
        QByteArray(kUsbCompositeDiagnosticPlTreeV1));
    claims->programmableLogicSourceClosureSha256 = QByteArray::fromHex(
        QByteArray(kUsbCompositeDiagnosticPlClosureSha256V1));
    claims->programmableLogicBitstreamSha256 = QByteArray::fromHex(
        QByteArray(kUsbCompositeDiagnosticPlBitstreamSha256V1));
}

Fixture validFixture()
{
    Fixture fixture;
    fixture.input.systemManifestObject = QByteArrayLiteral(
        "fixture-only independently signed system manifest object");
    fixture.input.detachedSignature = QByteArrayLiteral("fixture-signature");
    fixture.input.keyId = QStringLiteral("fixture-key-id");
    fixture.input.acquisitionProductDescriptorObject = descriptorFixture(
        QStringLiteral("product_descriptor_schema_00010006.hex"));

    UsbCompositeRuntimeManifestClaimsV1 &claims =
        fixture.verification.claims;
    claims.policyFlags = kUsbCompositePolicyProductApprovedV1;
    claims.statusPolicyVersion = 0x54455354U;
    claims.maximumAgeMs = {500U, 500U, 500U};
    claims.configurationGeneration = 73U;
    claims.manifestGeneration = 9U;
    for (int index = 0; index < 32; ++index) {
        claims.systemPackageSha256.append(static_cast<char>(index + 1));
        claims.configurationSha256.append(static_cast<char>(0xA0 + index));
    }
    claims.manifestSha256 = QCryptographicHash::hash(
        fixture.input.systemManifestObject, QCryptographicHash::Sha256);
    claims.acquisitionRegisterSchema =
        kUsbCompositeAcquisitionRegisterSchemaV1;
    claims.acquisitionProductDescriptorSha256 = QCryptographicHash::hash(
        fixture.input.acquisitionProductDescriptorObject,
        QCryptographicHash::Sha256);
    setProductPlIdentity(
        &claims, kUsbCompositePlProfile50m14V1,
        kUsbCompositeProductPl50m14BitstreamSha256V1);
    fixture.input.csp1PolicyObject = encodeCsp1(claims);

    fixture.verification.signatureDecisionKnown = true;
    fixture.verification.signatureValid = true;
    fixture.verification.freshnessDecisionKnown = true;
    fixture.verification.fresh = true;
    fixture.verification.claimsAvailable = true;
    fixture.verification.message = QStringLiteral("fixture verified");
    fixture.verification.verifierIdentity =
        QStringLiteral("FIXTURE_ONLY/verifier");
    fixture.verification.signatureScheme =
        QStringLiteral("FIXTURE_ONLY/signature-scheme");
    fixture.verification.policyIdentity =
        QStringLiteral("FIXTURE_ONLY/product-policy");
    fixture.verification.manifestIdentity =
        QStringLiteral("FIXTURE_ONLY/system-manifest");
    fixture.verification.configurationIdentity =
        QStringLiteral("FIXTURE_ONLY/configuration-generation-73");
    return fixture;
}

class FakeVerifier final : public UsbCompositeRuntimeManifestVerifierV1 {
public:
    explicit FakeVerifier(
        const UsbCompositeRuntimeManifestVerificationV1 &verification)
        : m_verification(verification)
    {
    }

    UsbCompositeRuntimeManifestVerificationV1 verify(
        const QByteArray &, const QByteArray &,
        const QString &) const override
    {
        return m_verification;
    }

private:
    UsbCompositeRuntimeManifestVerificationV1 m_verification;
};

QByteArray crs1Fixture()
{
    QFile file(QStringLiteral(
        UCM_D021_FIXTURE_DIR "/crs1_runtime_chain_ready.bin"));
    if (!file.open(QIODevice::ReadOnly)) {
        expect(false, QStringLiteral("locked CRS1 fixture opens"));
        return {};
    }
    return file.readAll();
}

UsbCompositeRuntimeTrustResultV1 install(
    UsbCompositeRuntimeTrustSourceV1 *source, const Fixture &fixture)
{
    const FakeVerifier verifier(fixture.verification);
    return source->install(fixture.input, verifier);
}

void expectRejectClears(
    UsbCompositeRuntimeTrustSourceV1 *source,
    const Fixture &valid,
    const Fixture &rejected,
    UsbCompositeRuntimeTrustCodeV1 expectedCode,
    const QString &name)
{
    const UsbCompositeRuntimeTrustResultV1 ready = install(source, valid);
    const UsbCompositeRuntimeTrustResultV1 result = install(source, rejected);
    expect(ready.success && !result.success && result.code == expectedCode
               && !source->ready() && source->policy() == nullptr
               && !result.evidence.value(QStringLiteral("ready")).toBool(),
           name);
}

void productPolicyRequiresIndependentTrust()
{
    const Fixture fixture = validFixture();
    UsbCompositeRuntimeTrustSourceV1 source;
    const UsbCompositeRuntimeTrustResultV1 accepted = install(&source, fixture);
    const UsbCompositeRuntimePolicyV1 *policy = source.policy();
    expect(accepted.success && source.ready() && policy != nullptr
               && policy->allowlisted
               && policy->statusPolicyVersion == 0x54455354U
               && policy->configurationGeneration == 73U
               && accepted.evidence.value(QStringLiteral("schema")).toString()
                   == QStringLiteral("UCM.WINDOWS.CRS1.TRUST.CANDIDATE.3")
               && accepted.evidence.value(QStringLiteral("explicit_injection"))
                      .toBool()
               && accepted.evidence.value(
                       QStringLiteral("arm_candidate_commit")).toString()
                   == QStringLiteral(
                       "e440037f415c1d2ae76f59227596ca9867bc45cb")
               && accepted.evidence.value(
                       QStringLiteral("arm_candidate_tree")).toString()
                   == QStringLiteral(
                       "dd837f231e8b839da47feb3ed8a5b711d2c2fcbe")
               && accepted.evidence.value(
                       QStringLiteral("acquisition_register_schema")).toString()
                   == QStringLiteral("0x00010006")
               && accepted.evidence.value(
                       QStringLiteral("programmable_logic_identity")).toString()
                   == QStringLiteral("PRODUCT_50M14")
               && accepted.evidence.value(
                       QStringLiteral("programmable_logic_source_commit")).toString()
                   == QString::fromLatin1(kUsbCompositeProductPlCommitV1)
               && accepted.evidence.value(QStringLiteral(
                       "programmable_logic_source_closure_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeProductPlClosureSha256V1)
               && accepted.evidence.value(QStringLiteral(
                       "programmable_logic_bitstream_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeProductPl50m14BitstreamSha256V1)
               && accepted.evidence.value(
                       QStringLiteral("programmable_logic_product_eligible"))
                      .toBool()
               && !accepted.evidence.value(
                       QStringLiteral("programmable_logic_tx_authorized")).toBool()
               && !accepted.evidence.value(
                       QStringLiteral("programmable_logic_hv_authorized")).toBool()
               && !accepted.evidence.value(QStringLiteral("crs1_self_approval"))
                       .toBool()
               && !accepted.evidence.value(
                       QStringLiteral("product_trust_material_embedded")).toBool()
               && accepted.evidence.value(
                       QStringLiteral("runtime_identity_schema_embedded")).toBool()
               && !accepted.evidence.value(
                       QStringLiteral("direct_driver_access")).toBool()
               && !accepted.evidence.value(
                       QStringLiteral("hardware_truth_authority")).toBool()
               && !accepted.evidence.value(
                       QStringLiteral("algorithm_truth_authority")).toBool()
               && accepted.evidence.value(
                       QStringLiteral("programmable_logic_identity_source"))
                      .toString()
                   == QStringLiteral("SIGNED_SYSTEM_MANIFEST_CLAIMS_ONLY"),
           QStringLiteral(
               "product-approved CSP1 activates only after independent manifest verification"));

    const QByteArray ready = crs1Fixture();
    const UsbCompositeRuntimeStatusResultV1 withoutTrust =
        makeUsbCompositeRuntimeStatusResultV1(ready, nullptr);
    const UsbCompositeRuntimeStatusResultV1 withTrust =
        makeUsbCompositeRuntimeStatusResultV1(ready, policy);
    const CompoundRuntimePresentation presentation =
        presentCompoundRuntimeStatus(withTrust.snapshot);
    expect(!withoutTrust.success
               && withoutTrust.code == UsbCompositeRuntimeDecodeCodeV1::Flags
               && withTrust.success && presentation.runtimeChainReady
               && !presentation.formalMeasurementCredit,
           QStringLiteral(
               "CRS1 cannot self-approve; injected trust grants runtime-chain only"));
}

void acquisitionSchemaMutualRejectionAudit()
{
    const QJsonObject rebindAudit = jsonFixture(
        QStringLiteral("arm_schema6_rebind_audit.json"));
    const QJsonObject previousSource = rebindAudit.value(
        QStringLiteral("previous_source")).toObject();
    const QJsonObject currentSource = rebindAudit.value(
        QStringLiteral("current_source")).toObject();
    const QJsonObject schemaAudit = rebindAudit.value(
        QStringLiteral("acquisition_schema")).toObject();
    const QJsonObject descriptorAudit = rebindAudit.value(
        QStringLiteral("product_descriptor")).toObject();
    const QJsonObject wireAudit = rebindAudit.value(
        QStringLiteral("d020_d021_wire")).toObject();
    expect(rebindAudit.value(QStringLiteral("status")).toString()
                   == QStringLiteral("BYTE_IDENTICAL_REBIND_ONLY")
               && previousSource.value(QStringLiteral("commit")).toString()
                   == QStringLiteral(
                       "212a963d9725000873da5f169cb3a737245b11c7")
               && currentSource.value(QStringLiteral("commit")).toString()
                   == QStringLiteral(
                       "e440037f415c1d2ae76f59227596ca9867bc45cb")
               && currentSource.value(QStringLiteral("tree")).toString()
                   == QStringLiteral(
                       "dd837f231e8b839da47feb3ed8a5b711d2c2fcbe")
               && schemaAudit.value(QStringLiteral("value")).toString()
                   == QStringLiteral("0x00010006")
               && schemaAudit.value(QStringLiteral("identical")).toBool()
               && descriptorAudit.value(QStringLiteral("identical")).toBool()
               && descriptorAudit.value(
                       QStringLiteral("decoded_sha256")).toString()
                   == QStringLiteral(
                       "b4647d2b4887311fbc3ad965251e27c0ce78862b167381de9805d6869898674b")
               && wireAudit.value(
                       QStringLiteral("wire_vector_count")).toInt() == 15
               && wireAudit.value(
                       QStringLiteral("wire_vector_differences")).toInt(-1) == 0
               && wireAudit.value(
                       QStringLiteral("wire_vectors_identical")).toBool()
               && wireAudit.value(
                       QStringLiteral("generator_git_blob_previous")).toString()
                   != wireAudit.value(
                       QStringLiteral("generator_git_blob_current")).toString(),
           QStringLiteral(
               "ARM 212a-to-e440 rebind proves unchanged schema, descriptor and all 15 wire vectors"));

    const QByteArray current = descriptorFixture(
        QStringLiteral("product_descriptor_schema_00010006.hex"));
    const QByteArray legacy = descriptorFixture(
        QStringLiteral("legacy_product_descriptor_schema_00010005.hex"));
    UsbCompositeAcquisitionIdentityV1 currentIdentity;
    UsbCompositeAcquisitionIdentityV1 legacyIdentity;
    QString error;
    const UsbCompositeRuntimeTrustCodeV1 currentCode =
        decodeUsbCompositeAcquisitionIdentityV1(
            current, &currentIdentity, &error);
    const UsbCompositeRuntimeTrustCodeV1 legacyCode =
        decodeUsbCompositeAcquisitionIdentityV1(
            legacy, &legacyIdentity, &error);
    expect(currentCode == UsbCompositeRuntimeTrustCodeV1::Ok
               && currentIdentity.registerSchema == 0x00010006U
               && currentIdentity.internalCrc32 == 0x7FE32515U
               && currentIdentity.descriptorSha256.toHex()
                   == QByteArrayLiteral(
                       "b4647d2b4887311fbc3ad965251e27c0ce78862b167381de9805d6869898674b")
               && currentIdentity.registerSchema != 0x00010005U,
           QStringLiteral(
               "ARM e440037 descriptor binds unchanged exact schema-6 identity and rejects schema-5 gate"));
    expect(legacyCode == UsbCompositeRuntimeTrustCodeV1::Ok
               && legacyIdentity.registerSchema == 0x00010005U
               && legacyIdentity.internalCrc32 == 0x555F959DU
               && legacyIdentity.descriptorSha256.toHex()
                   == QByteArrayLiteral(
                       "b1c6a3609a45c9268e416c0abefcbe50d0f18de11a5bc74900dc6d73a070be3b")
               && legacyIdentity.registerSchema
                   != kUsbCompositeAcquisitionRegisterSchemaV1,
           QStringLiteral(
               "legacy c14 descriptor is audit-only and rejected by schema-6 gate"));

    Fixture signedLegacy = validFixture();
    signedLegacy.input.acquisitionProductDescriptorObject = legacy;
    signedLegacy.verification.claims.acquisitionRegisterSchema = 0x00010005U;
    signedLegacy.verification.claims.acquisitionProductDescriptorSha256 =
        QCryptographicHash::hash(legacy, QCryptographicHash::Sha256);
    UsbCompositeRuntimeTrustSourceV1 source;
    const UsbCompositeRuntimeTrustResultV1 rejected =
        install(&source, signedLegacy);
    expect(!rejected.success
               && rejected.code
                   == UsbCompositeRuntimeTrustCodeV1::RuntimeIdentity
               && !source.ready(),
           QStringLiteral(
               "signed legacy schema-5 claims still cannot activate current trust"));

    QFile auditFile(QStringLiteral(
        UCM_D021_FIXTURE_DIR "/legacy_schema_00010005_audit.json"));
    const bool auditOpened = auditFile.open(QIODevice::ReadOnly);
    const QJsonObject audit = auditOpened
        ? QJsonDocument::fromJson(auditFile.readAll()).object()
        : QJsonObject {};
    expect(auditOpened
               && !audit.value(
                       QStringLiteral("accepted_by_current_windows")).toBool(true)
               && audit.value(QStringLiteral("status")).toString()
                   == QStringLiteral("REJECTION_AND_ROLLBACK_AUDIT_ONLY")
               && audit.value(
                       QStringLiteral("acquisition_register_schema")).toString()
                   == QStringLiteral("0x00010005")
               && audit.value(QStringLiteral("source_commit")).toString()
                   == QStringLiteral(
                       "c14f7cd125d18c569a482d26ec512f0747281816"),
           QStringLiteral(
               "superseded schema-5 vectors are retained only in explicit non-accepting audit manifest"));
}

void programmableLogicIdentityMutualRejectionAudit()
{
    const QJsonObject fixture = jsonFixture(
        QStringLiteral("pl_runtime_identities.json"));
    const QJsonObject product = fixture.value(
        QStringLiteral("product")).toObject();
    const QJsonObject profiles = product.value(
        QStringLiteral("profiles")).toObject();
    const QJsonObject product50 = profiles.value(
        QStringLiteral("50m14")).toObject();
    const QJsonObject product65 = profiles.value(
        QStringLiteral("65m14")).toObject();
    const QJsonObject diagnostic = fixture.value(
        QStringLiteral("safety_diagnostic")).toObject();
    const QJsonObject boundary = fixture.value(
        QStringLiteral("windows_boundary")).toObject();
    expect(fixture.value(QStringLiteral("status")).toString()
                   == QStringLiteral("SIGNED_SYSTEM_MANIFEST_CLAIMS_REQUIRED")
               && product.value(QStringLiteral("commit")).toString()
                   == QString::fromLatin1(kUsbCompositeProductPlCommitV1)
               && product.value(QStringLiteral("tree")).toString()
                   == QString::fromLatin1(kUsbCompositeProductPlTreeV1)
               && product.value(
                       QStringLiteral("source_closure_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeProductPlClosureSha256V1)
               && product50.value(
                       QStringLiteral("bitstream_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeProductPl50m14BitstreamSha256V1)
               && product65.value(
                       QStringLiteral("bitstream_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeProductPl65m14BitstreamSha256V1)
               && !product.value(QStringLiteral("board_qualified")).toBool(true)
               && !product.value(QStringLiteral("hil")).toBool(true)
               && !product.value(QStringLiteral("nvm")).toBool(true),
           QStringLiteral(
               "product PL fixture binds exact commit/tree/closure and both candidate bitstreams without board credit"));
    expect(diagnostic.value(QStringLiteral("role")).toString()
                   == QString::fromLatin1(
                       kUsbCompositePlRoleSafetyDiagnosticV1)
               && diagnostic.value(QStringLiteral("commit")).toString()
                   == QString::fromLatin1(kUsbCompositeDiagnosticPlCommitV1)
               && diagnostic.value(
                       QStringLiteral("source_closure_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeDiagnosticPlClosureSha256V1)
               && diagnostic.value(
                       QStringLiteral("bitstream_sha256")).toString()
                   == QString::fromLatin1(
                       kUsbCompositeDiagnosticPlBitstreamSha256V1)
               && diagnostic.value(QStringLiteral("display_only")).toBool()
               && !diagnostic.value(
                       QStringLiteral("product_eligible")).toBool(true)
               && !diagnostic.value(
                       QStringLiteral("tx_path_present")).toBool(true)
               && !diagnostic.value(
                       QStringLiteral("hv_path_present")).toBool(true)
               && !boundary.value(
                       QStringLiteral("direct_open_ioctl_mmap_driver_path"))
                      .toBool(true)
               && !boundary.value(
                       QStringLiteral("hardware_truth_authority")).toBool(true),
           QStringLiteral(
               "diagnostic PL fixture is independently identified as display-only no-TX/no-HV"));

    Fixture product50Fixture = validFixture();
    expect(classifyUsbCompositeProgrammableLogicIdentityV1(
               product50Fixture.verification.claims)
               == UsbCompositeProgrammableLogicIdentityV1::Product50m14,
           QStringLiteral("exact product 50m14 tuple classifies as product"));

    Fixture product65Fixture = validFixture();
    setProductPlIdentity(
        &product65Fixture.verification.claims,
        kUsbCompositePlProfile65m14V1,
        kUsbCompositeProductPl65m14BitstreamSha256V1);
    UsbCompositeRuntimeTrustSourceV1 product65Source;
    const UsbCompositeRuntimeTrustResultV1 product65Accepted =
        install(&product65Source, product65Fixture);
    expect(product65Accepted.success && product65Source.ready()
               && product65Accepted.evidence.value(
                       QStringLiteral("programmable_logic_identity")).toString()
                   == QStringLiteral("PRODUCT_65M14"),
           QStringLiteral("exact product 65m14 tuple can activate product trust"));

    Fixture diagnosticFixture = validFixture();
    setDiagnosticPlIdentity(&diagnosticFixture.verification.claims);
    UsbCompositeRuntimeTrustSourceV1 diagnosticSource;
    const UsbCompositeRuntimeTrustResultV1 diagnosticRejected =
        install(&diagnosticSource, diagnosticFixture);
    expect(!diagnosticRejected.success && !diagnosticSource.ready()
               && diagnosticRejected.code
                   == UsbCompositeRuntimeTrustCodeV1::ProductApproval
               && diagnosticRejected.evidence.value(
                       QStringLiteral("programmable_logic_identity")).toString()
                   == QStringLiteral(
                       "SAFETY_DIAGNOSTIC_50M14_NO_TX_NO_HV")
               && diagnosticRejected.evidence.value(
                       QStringLiteral("programmable_logic_diagnostic_only"))
                      .toBool()
               && !diagnosticRejected.evidence.value(
                       QStringLiteral("programmable_logic_product_eligible"))
                      .toBool(true)
               && !diagnosticRejected.evidence.value(
                       QStringLiteral("programmable_logic_tx_path_present"))
                      .toBool(true)
               && !diagnosticRejected.evidence.value(
                       QStringLiteral("programmable_logic_hv_path_present"))
                      .toBool(true),
           QStringLiteral(
               "exact safety diagnostic identity remains visible but cannot promote product trust"));

    Fixture crossBound = validFixture();
    crossBound.verification.claims.programmableLogicBitstreamSha256 =
        QByteArray::fromHex(
            QByteArray(kUsbCompositeDiagnosticPlBitstreamSha256V1));
    UsbCompositeRuntimeTrustSourceV1 crossBoundSource;
    const UsbCompositeRuntimeTrustResultV1 crossBoundRejected =
        install(&crossBoundSource, crossBound);
    expect(!crossBoundRejected.success && !crossBoundSource.ready()
               && crossBoundRejected.code
                   == UsbCompositeRuntimeTrustCodeV1::ProgrammableLogicIdentity,
           QStringLiteral(
               "product source tuple cannot borrow diagnostic bitstream identity"));

    Fixture disguisedDiagnostic = validFixture();
    setDiagnosticPlIdentity(&disguisedDiagnostic.verification.claims);
    disguisedDiagnostic.verification.claims.programmableLogicRole =
        QString::fromLatin1(kUsbCompositePlRoleProductV1);
    UsbCompositeRuntimeTrustSourceV1 disguisedSource;
    const UsbCompositeRuntimeTrustResultV1 disguisedRejected =
        install(&disguisedSource, disguisedDiagnostic);
    expect(!disguisedRejected.success && !disguisedSource.ready()
               && disguisedRejected.code
                   == UsbCompositeRuntimeTrustCodeV1::ProgrammableLogicIdentity,
           QStringLiteral(
               "diagnostic commit/tree/closure cannot be relabelled as product"));
}

void parserRejectsWithoutOutputMutation()
{
    Fixture fixture = validFixture();
    UsbCompositePolicyManifestV1 sentinel;
    sentinel.flags = 0xA5A5A5A5U;
    sentinel.manifestGeneration = 0x1122334455667788ULL;
    fixture.input.csp1PolicyObject[8] = static_cast<char>(
        fixture.input.csp1PolicyObject.at(8) ^ 1);
    QString error;
    const UsbCompositeRuntimeTrustCodeV1 code =
        decodeUsbCompositePolicyManifestV1(
            fixture.input.csp1PolicyObject, &sentinel, &error);
    expect(code == UsbCompositeRuntimeTrustCodeV1::Crc
               && sentinel.flags == 0xA5A5A5A5U
               && sentinel.manifestGeneration == 0x1122334455667788ULL,
           QStringLiteral("CSP1 decode failure preserves caller output"));
}

void everyTrustFailureClearsReady()
{
    const Fixture valid = validFixture();
    UsbCompositeRuntimeTrustSourceV1 source;

    Fixture bad = valid;
    bad.input.csp1PolicyObject.clear();
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::Argument,
                       QStringLiteral("empty trust input clears ready"));

    bad = valid;
    bad.input.acquisitionProductDescriptorObject.clear();
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::Argument,
                       QStringLiteral("empty product descriptor clears ready"));

    bad = valid;
    bad.input.csp1PolicyObject[8] = static_cast<char>(
        bad.input.csp1PolicyObject.at(8) ^ 1);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::Crc,
                       QStringLiteral("CSP1 CRC failure clears ready"));

    bad = valid;
    bad.input.csp1PolicyObject = encodeCsp1(
        bad.verification.claims, kUsbCompositePolicyTestOnlyV1);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ProductApproval,
                       QStringLiteral("TEST_ONLY source cannot activate product trust"));

    bad = valid;
    bad.input.acquisitionProductDescriptorObject[76] = static_cast<char>(
        bad.input.acquisitionProductDescriptorObject.at(76) ^ 1);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ProductDescriptor,
                       QStringLiteral("descriptor internal CRC failure clears ready"));

    bad = valid;
    bad.input.systemManifestObject.append('x');
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestHash,
                       QStringLiteral("system manifest hash mismatch clears ready"));

    bad = valid;
    bad.verification.signatureDecisionKnown = false;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::SignatureUnknown,
                       QStringLiteral("unknown signature decision clears ready"));

    bad = valid;
    bad.verification.signatureValid = false;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::SignatureInvalid,
                       QStringLiteral("invalid signature clears ready"));

    bad = valid;
    bad.verification.freshnessDecisionKnown = false;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::FreshnessUnknown,
                       QStringLiteral("unknown freshness clears ready"));

    bad = valid;
    bad.verification.fresh = false;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::Stale,
                       QStringLiteral("stale manifest clears ready"));

    bad = valid;
    bad.verification.claimsAvailable = false;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("missing verified claims clears ready"));

    bad = valid;
    bad.verification.claims.systemPackageSha256[0] = static_cast<char>(
        bad.verification.claims.systemPackageSha256.at(0) ^ 1);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("system package SHA claim mismatch clears ready"));

    bad = valid;
    bad.verification.claims.policyFlags = kUsbCompositePolicyTestOnlyV1;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("signed product-approval claim mismatch clears ready"));

    bad = valid;
    bad.verification.claims.configurationSha256[0] = static_cast<char>(
        bad.verification.claims.configurationSha256.at(0) ^ 1);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("configuration SHA claim mismatch clears ready"));

    bad = valid;
    bad.verification.claims.acquisitionRegisterSchema = 0x00010005U;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("signed acquisition schema mismatch clears ready"));

    bad = valid;
    bad.verification.claims.acquisitionProductDescriptorSha256[0] =
        static_cast<char>(
            bad.verification.claims.acquisitionProductDescriptorSha256.at(0)
            ^ 1);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("signed descriptor SHA mismatch clears ready"));

    bad = valid;
    bad.verification.claims.programmableLogicSourceCommit[0] =
        static_cast<char>(
            bad.verification.claims.programmableLogicSourceCommit.at(0) ^ 1);
    expectRejectClears(
        &source, valid, bad,
        UsbCompositeRuntimeTrustCodeV1::ProgrammableLogicIdentity,
        QStringLiteral("signed PL source commit mismatch clears ready"));

    bad = valid;
    setDiagnosticPlIdentity(&bad.verification.claims);
    expectRejectClears(
        &source, valid, bad,
        UsbCompositeRuntimeTrustCodeV1::ProductApproval,
        QStringLiteral("signed diagnostic-only PL identity clears product ready"));

    bad = valid;
    ++bad.verification.claims.statusPolicyVersion;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("policy version claim mismatch clears ready"));

    bad = valid;
    ++bad.verification.claims.maximumAgeMs[1];
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("per-record maximum age mismatch clears ready"));

    bad = valid;
    ++bad.verification.claims.configurationGeneration;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("configuration generation mismatch clears ready"));

    bad = valid;
    ++bad.verification.claims.manifestGeneration;
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::ManifestClaims,
                       QStringLiteral("manifest generation mismatch clears ready"));

    bad = valid;
    bad.input.csp1PolicyObject[175] = 1;
    sealCrc(&bad.input.csp1PolicyObject);
    expectRejectClears(&source, valid, bad,
                       UsbCompositeRuntimeTrustCodeV1::Reserved,
                       QStringLiteral("nonzero CSP1 reserved bytes clear ready"));
}

void generationWatermarkRejectsRollbackAndConflict()
{
    const Fixture valid = validFixture();
    UsbCompositeRuntimeTrustSourceV1 source;
    const UsbCompositeRuntimeTrustResultV1 first = install(&source, valid);
    const UsbCompositeRuntimeTrustResultV1 repeated = install(&source, valid);
    expect(first.success && repeated.success && source.ready(),
           QStringLiteral("identical CSP1 generation is idempotent"));

    Fixture older = valid;
    older.verification.claims.manifestGeneration = 8U;
    older.input.csp1PolicyObject = encodeCsp1(older.verification.claims);
    const UsbCompositeRuntimeTrustResultV1 stale = install(&source, older);
    expect(!stale.success
               && stale.code == UsbCompositeRuntimeTrustCodeV1::Stale
               && !source.ready(),
           QStringLiteral("lower manifest generation rejects and clears ready"));

    expect(install(&source, valid).success,
           QStringLiteral("accepted watermark object can be reinstalled"));
    Fixture conflict = valid;
    ++conflict.verification.claims.statusPolicyVersion;
    conflict.input.csp1PolicyObject = encodeCsp1(
        conflict.verification.claims);
    const UsbCompositeRuntimeTrustResultV1 duplicate =
        install(&source, conflict);
    expect(!duplicate.success
               && duplicate.code
                   == UsbCompositeRuntimeTrustCodeV1::DuplicateConflict
               && !source.ready(),
           QStringLiteral(
               "same generation with different CSP1 rejects and clears ready"));

    source.clear(QStringLiteral("fixture explicit clear"));
    expect(!source.ready() && source.policy() == nullptr
               && source.evidence().value(QStringLiteral("status")).toString()
                   == QStringLiteral("CLEARED")
               && source.evidence()
                      .value(QStringLiteral("highest_manifest_generation"))
                      .toString() == QStringLiteral("9"),
           QStringLiteral("explicit clear drops trust but retains rollback watermark"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    productPolicyRequiresIndependentTrust();
    acquisitionSchemaMutualRejectionAudit();
    programmableLogicIdentityMutualRejectionAudit();
    parserRejectsWithoutOutputMutation();
    everyTrustFailureClearsReady();
    generationWatermarkRejectsRollbackAndConflict();
    return failures == 0 ? 0 : 1;
}
