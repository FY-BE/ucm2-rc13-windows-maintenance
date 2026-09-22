#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ucm {

// Consumer/UI semantics. The ARM-authoritative CRS1 parser normalizes its
// exact wire contract into these fields and sets validation/consistency
// decisions explicitly; legacy URS2 is never reinterpreted as this model.
struct CompoundRuntimeSubstatus {
    bool present = false;
    bool identityAvailable = false;
    QString identity;
    bool identityBindingKnown = false;
    bool identityBound = false;
    bool generationAvailable = false;
    QString generation;
    bool heartbeatAvailable = false;
    QString heartbeat;
    bool stateKnown = false;
    bool stateHealthy = false;
    QString stateText;
    bool faultKnown = false;
    bool faultActive = false;
    QString faultText;
    bool ageKnown = false;
    bool fresh = false;
    QString ageText;
    bool connectivityKnown = false;
    bool connected = false;
};

struct CompoundRuntimeFreshnessPolicyStatus {
    bool present = false;
    bool identityAvailable = false;
    QString identity;
    bool versionAvailable = false;
    QString version;
    bool approvalKnown = false;
    bool approved = false;
    bool configurationBindingKnown = false;
    bool configurationBound = false;
    QString configurationIdentity;
    bool manifestBindingKnown = false;
    bool manifestBound = false;
    QString manifestIdentity;
};

struct CompoundRuntimeSnapshot {
    bool authoritativeContractBound = false;
    bool available = false;
    bool sourceBytesValidated = false;
    QString message;
    bool contractVersionKnown = false;
    bool contractVersionSupported = false;
    QString contractVersion;
    bool snapshotIdentityAvailable = false;
    QString snapshotIdentity;
    bool snapshotGenerationAvailable = false;
    QString snapshotGeneration;
    bool configurationGenerationAvailable = false;
    QString configurationGeneration;
    bool systemPackageIdentityAvailable = false;
    QString systemPackageSha256;
    bool configurationIdentityAvailable = false;
    QString configurationSha256;
    bool configurationBindingKnown = false;
    bool configurationBound = false;
    // CRS1 has no snapshot-age field. A future contract may provide one;
    // until then only the three authoritative per-record ages are gated.
    bool snapshotAgeKnown = false;
    bool snapshotFresh = false;
    QString snapshotAgeText;
    bool generationConsistencyKnown = false;
    bool generationConsistent = false;
    bool runtimeChainClaimKnown = false;
    bool runtimeChainClaim = false;
    CompoundRuntimeFreshnessPolicyStatus freshnessPolicy;
    CompoundRuntimeSubstatus acquisitiond;
    CompoundRuntimeSubstatus measurementd;
    CompoundRuntimeSubstatus cmLink;
};

enum class CompoundRuntimeDisplayState {
    AwaitingAuthoritativeContract,
    Unavailable,
    InvalidSource,
    UnsupportedVersion,
    Incomplete,
    FreshnessPolicyNotReady,
    Stale,
    GenerationMismatch,
    LinkDisconnected,
    Faulted,
    NotOperational,
    Operational
};

struct CompoundRuntimeComponentPresentation {
    QString key;
    QString label;
    bool complete = false;
    bool healthy = false;
    QString stateText;
    QString detailText;
};

struct CompoundRuntimePresentation {
    CompoundRuntimeDisplayState state =
        CompoundRuntimeDisplayState::AwaitingAuthoritativeContract;
    // This is the only positive conclusion the compound status may grant.
    // It is forced false for every fail-closed presentation state.
    bool runtimeChainReady = false;
    // Compound runtime health never grants formal-measurement credit. That
    // remains gated by the separately validated measurement snapshot.
    bool formalMeasurementCredit = false;
    QString stateText;
    QString detailText;
    QVector<CompoundRuntimeComponentPresentation> components;
};

CompoundRuntimePresentation presentCompoundRuntimeStatus(
    const CompoundRuntimeSnapshot &snapshot);

QString compoundRuntimeDisplayStateCode(
    CompoundRuntimeDisplayState state);

QJsonObject compoundRuntimeDiagnosticJson(
    const CompoundRuntimeSnapshot &snapshot,
    const CompoundRuntimePresentation &presentation);

} // namespace ucm
