#include "usb_composite_runtime_status_v1.h"
#include "usb_extended_wire_v2.h"
#include "usb_wire_v1.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonObject>
#include <QTextStream>

using namespace ucm;

namespace {

int failures = 0;

void expect(bool condition, const QString &name)
{
    QTextStream(stdout) << (condition ? "PASS  " : "FAIL  ")
                        << name << '\n';
    if (!condition) ++failures;
}

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(UCM_D021_FIXTURE_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        expect(false, QStringLiteral("fixture opens: %1").arg(name));
        return {};
    }
    return file.readAll();
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

UsbCompositeRuntimePolicyV1 fixturePolicy()
{
    UsbCompositeRuntimePolicyV1 policy;
    policy.allowlisted = true;
    policy.identity = QStringLiteral("TEST_ONLY/status-policy-0x54455354");
    policy.manifestIdentity =
        QStringLiteral("TEST_ONLY/f478ec6755205f55890cb1fde33238178d7c8c04");
    policy.configurationIdentity =
        QStringLiteral("TEST_ONLY/configuration-sha256-a0-bf");
    policy.statusPolicyVersion = 0x54455354U;
    policy.maximumAgeMs = {500U, 500U, 500U};
    policy.configurationGeneration = 73U;
    for (int index = 0; index < 32; ++index) {
        policy.systemPackageSha256.append(static_cast<char>(index + 1));
        policy.configurationSha256.append(static_cast<char>(0xA0 + index));
    }
    return policy;
}

void lockedCopiesMatchArmOrigin()
{
    const struct Expected {
        const char *name;
        int bytes;
        const char *sha;
    } expected[] = {
        {"contract.json", 7789,
         "b189081d5460d0cc2c6317df5171c799ee669ab0b9b54afb0dbb872f0e10e820"},
        {"crs1_runtime_chain_ready.bin", 384,
         "286b993fa0688215b61880a41095052a40a5984d6bb1271ff52ed30b49216994"},
        {"crs1_stale_link_not_ready.bin", 384,
         "66df7620947a8fdde7c013e05f60998a9ce27905bcd6874f3a9fa24def107d02"},
        {"reject_crs1_bad_crc.bin", 384,
         "9df30b69097b9b66783d65047a332eed7942e85c8ef02714a264e034c4b17d40"},
        {"reject_crs1_claim_ready_stale_link.bin", 384,
         "e22c68ab99c7f8e0f184b218afcfd871fe7c01f23d3f76e1d900100a2563b090"},
        {"arm_schema6_rebind_audit.json", 2301,
         "5dc3bf574aecadf20cf19840c3c0aafa4ff8b0bebcdf4a7354bde139abd75587"},
        {"pl_runtime_identities.json", 1946,
         "e34492d9cfd7d30a21bc3b99d9b0629edd0d5b69efbe8c335c4b3f2e5f9ba760"}
    };
    for (const Expected &entry : expected) {
        const QByteArray bytes = fixture(QString::fromLatin1(entry.name));
        expect(bytes.size() == entry.bytes
                   && sha256(bytes) == QString::fromLatin1(entry.sha),
               QStringLiteral("locked ARM bytes and SHA-256: %1")
                   .arg(QString::fromLatin1(entry.name)));
    }
}

void readyVectorDecodesAndGrantsRuntimeChainOnly()
{
    const UsbCompositeRuntimePolicyV1 policy = fixturePolicy();
    const UsbCompositeRuntimeStatusResultV1 result =
        makeUsbCompositeRuntimeStatusResultV1(
            fixture(QStringLiteral("crs1_runtime_chain_ready.bin")),
            &policy);
    const CompoundRuntimePresentation presentation =
        presentCompoundRuntimeStatus(result.snapshot);
    const QJsonObject diagnostic = compoundRuntimeDiagnosticJson(
        result.snapshot, presentation);

    expect(result.success
               && result.code == UsbCompositeRuntimeDecodeCodeV1::Ok
               && result.status.schemaVersion == 1U
               && result.status.flags == 0x0FU
               && result.status.snapshotGeneration == 41U
               && result.status.configurationGeneration == 73U
               && result.status.statusPolicyVersion == 0x54455354U,
           QStringLiteral("CRS1 ready header and policy fields decode exactly"));
    expect(result.status.records.at(0).kind == 1U
               && result.status.records.at(1).kind == 2U
               && result.status.records.at(2).kind == 3U
               && result.status.records.at(0).bindingCrc32 != 0U
               && result.status.records.at(0).bindingCrc32
                   == result.status.records.at(1).bindingCrc32
               && result.status.records.at(1).bindingCrc32
                   == result.status.records.at(2).bindingCrc32,
           QStringLiteral("three fixed 80-byte records retain role and binding"));
    expect(presentation.state == CompoundRuntimeDisplayState::Operational
               && presentation.runtimeChainReady
               && !presentation.formalMeasurementCredit
               && diagnostic.value(QStringLiteral("runtime_chain_ready"))
                      .toBool()
               && !diagnostic
                       .value(QStringLiteral("formal_measurement_credit"))
                       .toBool(),
           QStringLiteral(
               "validated CRS1 grants RUNTIME_CHAIN_READY and never formal credit"));
}

void staleVectorRemainsValidButNotReady()
{
    const UsbCompositeRuntimePolicyV1 policy = fixturePolicy();
    const UsbCompositeRuntimeStatusResultV1 result =
        makeUsbCompositeRuntimeStatusResultV1(
            fixture(QStringLiteral("crs1_stale_link_not_ready.bin")),
            &policy);
    const CompoundRuntimePresentation presentation =
        presentCompoundRuntimeStatus(result.snapshot);
    expect(result.success && result.status.flags == 0x0BU
               && result.status.records.at(2).ageMs == 501U
               && result.status.records.at(2).maximumAgeMs == 500U
               && (result.status.records.at(2).flags
                   & kUsbCompositeRecordStaleV1) != 0U
               && presentation.state == CompoundRuntimeDisplayState::Stale
               && !presentation.runtimeChainReady,
           QStringLiteral(
               "authoritative stale link is diagnostic-valid and runtime-not-ready"));
}

void authoritativeNegativeVectorsFailWithoutOverwritingOutput()
{
    const UsbCompositeRuntimePolicyV1 policy = fixturePolicy();
    UsbCompositeRuntimeStatusV1 sentinel;
    sentinel.flags = 0xA5A5A5A5U;
    sentinel.snapshotGeneration = 0x1122334455667788ULL;
    QString error;

    UsbCompositeRuntimeDecodeCodeV1 code =
        decodeUsbCompositeRuntimeStatusV1(
            fixture(QStringLiteral("reject_crs1_bad_crc.bin")), &policy,
            &sentinel, &error);
    expect(code == UsbCompositeRuntimeDecodeCodeV1::Crc
               && sentinel.flags == 0xA5A5A5A5U
               && sentinel.snapshotGeneration == 0x1122334455667788ULL,
           QStringLiteral("ARM bad-CRC vector rejects without output mutation"));

    code = decodeUsbCompositeRuntimeStatusV1(
        fixture(QStringLiteral("reject_crs1_claim_ready_stale_link.bin")),
        &policy, &sentinel, &error);
    expect(code == UsbCompositeRuntimeDecodeCodeV1::Flags
               && sentinel.flags == 0xA5A5A5A5U,
           QStringLiteral(
               "ARM illegal ready-claim vector rejects without output mutation"));
}

void localPolicyAndDispatchBoundariesStayClosed()
{
    const QByteArray ready = fixture(
        QStringLiteral("crs1_runtime_chain_ready.bin"));
    UsbCompositeRuntimeStatusV1 sentinel;
    sentinel.flags = 0xCCU;
    QString error;
    UsbCompositeRuntimeDecodeCodeV1 code =
        decodeUsbCompositeRuntimeStatusV1(
            ready, nullptr, &sentinel, &error);
    expect(code == UsbCompositeRuntimeDecodeCodeV1::Flags
               && sentinel.flags == 0xCCU,
           QStringLiteral(
               "wire-approved TEST_ONLY policy is rejected without local allowlist"));

    UsbCompositeRuntimePolicyV1 policy = fixturePolicy();
    policy.configurationSha256[0] = static_cast<char>(
        policy.configurationSha256.at(0) ^ 1);
    code = decodeUsbCompositeRuntimeStatusV1(
        ready, &policy, &sentinel, &error);
    expect(code == UsbCompositeRuntimeDecodeCodeV1::Flags,
           QStringLiteral(
               "configuration/manifest policy mismatch rejects wire approval"));

    policy = fixturePolicy();
    code = decodeUsbCompositeRuntimeStatusV1(
        ready.left(383), &policy, &sentinel, &error);
    expect(code == UsbCompositeRuntimeDecodeCodeV1::Length,
           QStringLiteral("383-byte short read is not accepted as CRS1"));

    QByteArray wrongSchema = ready;
    wrongSchema[4] = 2;
    code = decodeUsbCompositeRuntimeStatusV1(
        wrongSchema, &policy, &sentinel, &error);
    expect(code == UsbCompositeRuntimeDecodeCodeV1::Version,
           QStringLiteral("unknown CRS schema fails before semantic display"));

    expect(static_cast<quint16>(UsbMessageTypeV1::CompositeRuntimeStatusV1)
                   == 23U
               && kUsbExtendedFeatureCompositeRuntimeStatusV1
                   == (1ULL << 7),
           QStringLiteral("Windows dispatch constants match frozen type 23 / bit 7"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    lockedCopiesMatchArmOrigin();
    readyVectorDecodesAndGrantsRuntimeChainOnly();
    staleVectorRemainsValidButNotReady();
    authoritativeNegativeVectorsFailWithoutOverwritingOutput();
    localPolicyAndDispatchBoundariesStayClosed();
    return failures == 0 ? 0 : 1;
}
