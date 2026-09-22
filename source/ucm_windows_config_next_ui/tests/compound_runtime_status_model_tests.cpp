#include "compound_runtime_status_model.h"

#include <QCoreApplication>
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

CompoundRuntimeSubstatus healthyComponent(const QString &name,
                                          bool link = false)
{
    CompoundRuntimeSubstatus status;
    status.present = true;
    status.identityAvailable = true;
    status.identity = name + QStringLiteral("-identity");
    status.identityBindingKnown = true;
    status.identityBound = true;
    status.generationAvailable = true;
    status.generation = name + QStringLiteral("-generation");
    status.heartbeatAvailable = true;
    status.heartbeat = name + QStringLiteral("-heartbeat");
    status.stateKnown = true;
    status.stateHealthy = true;
    status.stateText = QStringLiteral("authoritative healthy state");
    status.faultKnown = true;
    status.faultActive = false;
    status.faultText = QStringLiteral("none");
    status.ageKnown = true;
    status.fresh = true;
    status.ageText = name + QStringLiteral("-authoritative-age");
    if (link) {
        status.connectivityKnown = true;
        status.connected = true;
    }
    return status;
}

CompoundRuntimeSnapshot healthySnapshot()
{
    CompoundRuntimeSnapshot snapshot;
    snapshot.authoritativeContractBound = true;
    snapshot.available = true;
    snapshot.sourceBytesValidated = true;
    snapshot.contractVersionKnown = true;
    snapshot.contractVersionSupported = true;
    snapshot.contractVersion = QStringLiteral("arm-authoritative-test-version");
    snapshot.snapshotIdentityAvailable = true;
    snapshot.snapshotIdentity = QStringLiteral("same-snapshot-identity");
    snapshot.snapshotGenerationAvailable = true;
    snapshot.snapshotGeneration = QStringLiteral("authoritative-generation");
    snapshot.configurationGenerationAvailable = true;
    snapshot.configurationGeneration =
        QStringLiteral("authoritative-configuration-generation");
    snapshot.systemPackageIdentityAvailable = true;
    snapshot.systemPackageSha256 =
        QStringLiteral("authoritative-system-package-sha256");
    snapshot.configurationIdentityAvailable = true;
    snapshot.configurationSha256 =
        QStringLiteral("authoritative-configuration-sha256");
    snapshot.configurationBindingKnown = true;
    snapshot.configurationBound = true;
    snapshot.snapshotAgeKnown = true;
    snapshot.snapshotFresh = true;
    snapshot.snapshotAgeText = QStringLiteral("authoritative-snapshot-age");
    snapshot.generationConsistencyKnown = true;
    snapshot.generationConsistent = true;
    snapshot.runtimeChainClaimKnown = true;
    snapshot.runtimeChainClaim = true;
    snapshot.freshnessPolicy.present = true;
    snapshot.freshnessPolicy.identityAvailable = true;
    snapshot.freshnessPolicy.identity =
        QStringLiteral("authoritative-policy-identity");
    snapshot.freshnessPolicy.versionAvailable = true;
    snapshot.freshnessPolicy.version =
        QStringLiteral("authoritative-policy-version");
    snapshot.freshnessPolicy.approvalKnown = true;
    snapshot.freshnessPolicy.approved = true;
    snapshot.freshnessPolicy.configurationBindingKnown = true;
    snapshot.freshnessPolicy.configurationBound = true;
    snapshot.freshnessPolicy.configurationIdentity =
        QStringLiteral("bound-configuration-identity");
    snapshot.freshnessPolicy.manifestBindingKnown = true;
    snapshot.freshnessPolicy.manifestBound = true;
    snapshot.freshnessPolicy.manifestIdentity =
        QStringLiteral("bound-manifest-identity");
    snapshot.acquisitiond = healthyComponent(QStringLiteral("acquisitiond"));
    snapshot.measurementd = healthyComponent(QStringLiteral("measurementd"));
    snapshot.cmLink = healthyComponent(QStringLiteral("cm-link"), true);
    return snapshot;
}

void approvedCompoundStateCanBecomeHealthyOnlyWhenComplete()
{
    const CompoundRuntimePresentation view =
        presentCompoundRuntimeStatus(healthySnapshot());
    expect(view.state == CompoundRuntimeDisplayState::Operational
               && view.runtimeChainReady
               && !view.formalMeasurementCredit
               && view.components.size() == 3,
           QStringLiteral(
               "complete authoritative acquisitiond/measurementd/link snapshot grants RUNTIME_CHAIN_READY only"));
}

void authorityAndVersionFailuresStayClosed()
{
    CompoundRuntimeSnapshot snapshot = healthySnapshot();
    snapshot.authoritativeContractBound = false;
    CompoundRuntimePresentation view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state
               == CompoundRuntimeDisplayState::AwaitingAuthoritativeContract
               && !view.runtimeChainReady
               && view.stateText.contains(QStringLiteral("ARM")),
           QStringLiteral("unbound ARM contract is explicitly blocked"));

    snapshot = healthySnapshot();
    snapshot.sourceBytesValidated = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::InvalidSource
               && !view.runtimeChainReady,
           QStringLiteral("unvalidated source bytes cannot feed UI health"));

    snapshot = healthySnapshot();
    snapshot.contractVersionKnown = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::UnsupportedVersion
               && !view.runtimeChainReady,
           QStringLiteral("unknown contract version fails closed"));

    snapshot = healthySnapshot();
    snapshot.contractVersionSupported = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::UnsupportedVersion
               && !view.runtimeChainReady,
           QStringLiteral("unsupported contract version fails closed"));

    snapshot = healthySnapshot();
    snapshot.available = false;
    snapshot.message = QStringLiteral("authoritative object unavailable");
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::Unavailable
               && !view.runtimeChainReady,
           QStringLiteral("unavailable current snapshot clears RUNTIME_CHAIN_READY"));
}

void missingStaleAndGenerationMismatchStayClosed()
{
    CompoundRuntimeSnapshot snapshot = healthySnapshot();
    snapshot.measurementd.present = false;
    CompoundRuntimePresentation view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::Incomplete
               && !view.runtimeChainReady
               && view.detailText.contains(QStringLiteral("measurementd")),
           QStringLiteral("missing daemon substatus is incomplete"));

    snapshot = healthySnapshot();
    snapshot.measurementd.fresh = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::Stale
               && !view.runtimeChainReady,
           QStringLiteral("one stale substatus invalidates the whole snapshot"));

    snapshot = healthySnapshot();
    snapshot.generationConsistent = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::GenerationMismatch
               && !view.runtimeChainReady
               && view.detailText.contains(QStringLiteral("ARM")),
           QStringLiteral(
               "authoritative generation mismatch fails without Windows-side equality guesses"));
}

void linkAndDaemonFailuresStayClosed()
{
    CompoundRuntimeSnapshot snapshot = healthySnapshot();
    snapshot.cmLink.connected = false;
    CompoundRuntimePresentation view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::LinkDisconnected
               && !view.runtimeChainReady,
           QStringLiteral("live daemons cannot mask a disconnected C-M link"));

    snapshot = healthySnapshot();
    snapshot.acquisitiond.faultActive = true;
    snapshot.acquisitiond.faultText = QStringLiteral("authoritative fault");
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::Faulted
               && !view.runtimeChainReady,
           QStringLiteral("acquisitiond fault revokes runtime-path health"));

    snapshot = healthySnapshot();
    snapshot.measurementd.faultActive = true;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::Faulted
               && !view.runtimeChainReady,
           QStringLiteral("measurementd fault revokes runtime-path health"));

    snapshot = healthySnapshot();
    snapshot.measurementd.stateHealthy = false;
    snapshot.measurementd.stateText = QStringLiteral("known non-operational");
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::NotOperational
               && !view.runtimeChainReady,
           QStringLiteral("known non-operational state never appears healthy"));

    snapshot = healthySnapshot();
    snapshot.cmLink.faultActive = true;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::Faulted
               && !view.runtimeChainReady,
           QStringLiteral("C-M link fault revokes runtime-path health"));

    snapshot = healthySnapshot();
    snapshot.runtimeChainClaim = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::NotOperational
               && !view.runtimeChainReady
               && !view.formalMeasurementCredit,
           QStringLiteral(
               "authoritative false runtime-chain claim cannot appear ready"));

    snapshot = healthySnapshot();
    snapshot.measurementd.identityBound = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::NotOperational
               && !view.runtimeChainReady,
           QStringLiteral("unbound daemon identity cannot appear ready"));

    snapshot = healthySnapshot();
    snapshot.configurationBound = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state == CompoundRuntimeDisplayState::NotOperational
               && !view.runtimeChainReady,
           QStringLiteral("unbound configuration cannot appear ready"));
}

void freshnessPolicyMustBeApprovedAndBound()
{
    CompoundRuntimeSnapshot snapshot = healthySnapshot();
    snapshot.freshnessPolicy.present = false;
    CompoundRuntimePresentation view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state
                   == CompoundRuntimeDisplayState::FreshnessPolicyNotReady
               && !view.runtimeChainReady && view.components.size() == 3,
           QStringLiteral(
               "missing freshness policy keeps diagnostics but blocks RUNTIME_CHAIN_READY"));

    snapshot = healthySnapshot();
    snapshot.freshnessPolicy.versionAvailable = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state
                   == CompoundRuntimeDisplayState::FreshnessPolicyNotReady
               && !view.runtimeChainReady,
           QStringLiteral("unknown freshness policy version fails closed"));

    snapshot = healthySnapshot();
    snapshot.freshnessPolicy.approved = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state
                   == CompoundRuntimeDisplayState::FreshnessPolicyNotReady
               && !view.runtimeChainReady,
           QStringLiteral("non-allowlisted freshness policy fails closed"));

    snapshot = healthySnapshot();
    snapshot.freshnessPolicy.configurationBound = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state
                   == CompoundRuntimeDisplayState::FreshnessPolicyNotReady
               && !view.runtimeChainReady,
           QStringLiteral("unbound configuration policy fails closed"));

    snapshot = healthySnapshot();
    snapshot.freshnessPolicy.manifestBound = false;
    view = presentCompoundRuntimeStatus(snapshot);
    expect(view.state
                   == CompoundRuntimeDisplayState::FreshnessPolicyNotReady
               && !view.runtimeChainReady,
           QStringLiteral("unbound system-manifest policy fails closed"));
}

void diagnosticShapeIsVersionedAndPreservesAllThreeDomains()
{
    const CompoundRuntimeSnapshot snapshot = healthySnapshot();
    const CompoundRuntimePresentation view =
        presentCompoundRuntimeStatus(snapshot);
    const QJsonObject json = compoundRuntimeDiagnosticJson(snapshot, view);
    const QJsonObject acquisitiond =
        json.value(QStringLiteral("acquisitiond")).toObject();
    const QJsonObject measurementd =
        json.value(QStringLiteral("measurementd")).toObject();
    const QJsonObject link = json.value(QStringLiteral("cm_link")).toObject();
    const auto hasRequiredFields = [](const QJsonObject &object) {
        return object.contains(QStringLiteral("identity"))
            && object.contains(QStringLiteral("generation"))
            && object.contains(QStringLiteral("heartbeat"))
            && object.contains(QStringLiteral("state"))
            && object.contains(QStringLiteral("fault"))
            && object.contains(QStringLiteral("age"));
    };
    expect(json.value(QStringLiteral("diagnostic_schema")).toString()
                   == QStringLiteral(
                       "UCM.WINDOWS.COMPOUND_RUNTIME.DIAGNOSTIC.1")
               && json.value(QStringLiteral("parser_status")).toString()
                   == QStringLiteral("ARM_CONTRACT_VALIDATED")
               && json.value(QStringLiteral("runtime_chain_ready")).toBool()
               && !json.value(QStringLiteral("formal_measurement_credit"))
                       .toBool()
               && !json.value(QStringLiteral("legacy_urs2_reinterpreted"))
                       .toBool()
               && hasRequiredFields(acquisitiond)
               && hasRequiredFields(measurementd)
               && hasRequiredFields(link)
               && json.value(QStringLiteral("freshness_policy"))
                      .toObject()
                      .value(QStringLiteral("approval"))
                      .toObject()
                      .value(QStringLiteral("approved"))
                      .toBool()
               && link.contains(QStringLiteral("connectivity")),
           QStringLiteral(
               "diagnostic v1 keeps snapshot, both daemons, link, and no-formal boundary"));

    const CompoundRuntimeSnapshot blocked;
    const QJsonObject blockedJson = compoundRuntimeDiagnosticJson(
        blocked, presentCompoundRuntimeStatus(blocked));
    expect(blockedJson.value(QStringLiteral("parser_status")).toString()
                   == QStringLiteral("BLOCKED_ARM_SCHEMA")
               && blockedJson.value(QStringLiteral("overall_state")).toString()
                   == QStringLiteral("AWAITING_ARM_CONTRACT")
               && !blockedJson.value(QStringLiteral("runtime_chain_ready"))
                       .toBool(),
           QStringLiteral(
               "diagnostic export exposes parser blocker instead of guessed wire data"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    approvedCompoundStateCanBecomeHealthyOnlyWhenComplete();
    authorityAndVersionFailuresStayClosed();
    missingStaleAndGenerationMismatchStayClosed();
    linkAndDaemonFailuresStayClosed();
    freshnessPolicyMustBeApprovedAndBound();
    diagnosticShapeIsVersionedAndPreservesAllThreeDomains();
    return failures == 0 ? 0 : 1;
}
