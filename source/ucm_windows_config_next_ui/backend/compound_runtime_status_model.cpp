#include "compound_runtime_status_model.h"

#include <QJsonArray>
#include <QStringList>

namespace ucm {
namespace {

bool hasValue(bool available, const QString &value)
{
    return available && !value.trimmed().isEmpty();
}

bool componentComplete(const CompoundRuntimeSubstatus &status,
                       bool requireConnectivity)
{
    return status.present
        && hasValue(status.identityAvailable, status.identity)
        && status.identityBindingKnown
        && hasValue(status.generationAvailable, status.generation)
        && hasValue(status.heartbeatAvailable, status.heartbeat)
        && status.stateKnown && status.faultKnown
        && hasValue(status.ageKnown, status.ageText)
        && (!requireConnectivity || status.connectivityKnown);
}

CompoundRuntimeComponentPresentation componentPresentation(
    const QString &key, const QString &label,
    const CompoundRuntimeSubstatus &status, bool requireConnectivity)
{
    CompoundRuntimeComponentPresentation result;
    result.key = key;
    result.label = label;
    result.complete = componentComplete(status, requireConnectivity);
    result.healthy = result.complete && status.fresh
        && status.identityBound && status.stateHealthy && !status.faultActive
        && (!requireConnectivity || status.connected);
    result.stateText = status.stateKnown && !status.stateText.isEmpty()
        ? status.stateText : QStringLiteral("状态未知");

    QStringList details;
    details << (hasValue(status.identityAvailable, status.identity)
                    ? QStringLiteral("identity=%1").arg(status.identity)
                    : QStringLiteral("identity=缺失"));
    details << (status.identityBindingKnown
                    ? (status.identityBound
                           ? QStringLiteral("identity_binding=bound")
                           : QStringLiteral("identity_binding=unbound"))
                    : QStringLiteral("identity_binding=未知"));
    details << (hasValue(status.generationAvailable, status.generation)
                    ? QStringLiteral("generation=%1").arg(status.generation)
                    : QStringLiteral("generation=缺失"));
    details << (hasValue(status.heartbeatAvailable, status.heartbeat)
                    ? QStringLiteral("heartbeat=%1").arg(status.heartbeat)
                    : QStringLiteral("heartbeat=缺失"));
    details << (hasValue(status.ageKnown, status.ageText)
                    ? QStringLiteral("age=%1/%2")
                          .arg(status.ageText,
                               status.fresh ? QStringLiteral("fresh")
                                            : QStringLiteral("stale"))
                    : QStringLiteral("age=未知"));
    details << (status.faultKnown
                    ? (status.faultActive
                           ? QStringLiteral("fault=%1").arg(
                                 status.faultText.isEmpty()
                                     ? QStringLiteral("active")
                                     : status.faultText)
                           : QStringLiteral("fault=none"))
                    : QStringLiteral("fault=未知"));
    if (requireConnectivity) {
        details << (status.connectivityKnown
                        ? (status.connected
                               ? QStringLiteral("link=connected")
                               : QStringLiteral("link=disconnected"))
                        : QStringLiteral("link=未知"));
    }
    result.detailText = details.join(QStringLiteral(" · "));
    return result;
}

QJsonObject textField(bool available, const QString &value)
{
    return {
        {QStringLiteral("available"), available},
        {QStringLiteral("value"), available ? QJsonValue(value)
                                             : QJsonValue(QJsonValue::Null)}
    };
}

QJsonObject componentJson(const CompoundRuntimeSubstatus &status,
                          const CompoundRuntimeComponentPresentation &view,
                          bool includeConnectivity)
{
    QJsonObject result {
        {QStringLiteral("present"), status.present},
        {QStringLiteral("complete"), view.complete},
        {QStringLiteral("healthy"), view.healthy},
        {QStringLiteral("identity"),
         textField(status.identityAvailable, status.identity)},
        {QStringLiteral("identity_binding"), QJsonObject {
             {QStringLiteral("known"), status.identityBindingKnown},
             {QStringLiteral("bound"), status.identityBound}
         }},
        {QStringLiteral("generation"),
         textField(status.generationAvailable, status.generation)},
        {QStringLiteral("heartbeat"),
         textField(status.heartbeatAvailable, status.heartbeat)},
        {QStringLiteral("state"), QJsonObject {
             {QStringLiteral("known"), status.stateKnown},
             {QStringLiteral("healthy"), status.stateHealthy},
             {QStringLiteral("label"), status.stateText}
         }},
        {QStringLiteral("fault"), QJsonObject {
             {QStringLiteral("known"), status.faultKnown},
             {QStringLiteral("active"), status.faultActive},
             {QStringLiteral("label"), status.faultText}
         }},
        {QStringLiteral("age"), QJsonObject {
             {QStringLiteral("known"), status.ageKnown},
             {QStringLiteral("fresh"), status.fresh},
             {QStringLiteral("display"), status.ageText}
         }}
    };
    if (includeConnectivity) {
        result.insert(QStringLiteral("connectivity"), QJsonObject {
            {QStringLiteral("known"), status.connectivityKnown},
            {QStringLiteral("connected"), status.connected}
        });
    }
    return result;
}

bool policyComplete(const CompoundRuntimeFreshnessPolicyStatus &policy)
{
    return policy.present
        && hasValue(policy.identityAvailable, policy.identity)
        && hasValue(policy.versionAvailable, policy.version)
        && policy.approvalKnown
        && policy.configurationBindingKnown
        && policy.manifestBindingKnown
        && !policy.configurationIdentity.trimmed().isEmpty()
        && !policy.manifestIdentity.trimmed().isEmpty();
}

QJsonObject policyJson(const CompoundRuntimeFreshnessPolicyStatus &policy)
{
    return {
        {QStringLiteral("present"), policy.present},
        {QStringLiteral("identity"),
         textField(policy.identityAvailable, policy.identity)},
        {QStringLiteral("version"),
         textField(policy.versionAvailable, policy.version)},
        {QStringLiteral("approval"), QJsonObject {
             {QStringLiteral("known"), policy.approvalKnown},
             {QStringLiteral("approved"), policy.approved}
         }},
        {QStringLiteral("configuration_binding"), QJsonObject {
             {QStringLiteral("known"), policy.configurationBindingKnown},
             {QStringLiteral("bound"), policy.configurationBound},
             {QStringLiteral("identity"), policy.configurationIdentity}
         }},
        {QStringLiteral("manifest_binding"), QJsonObject {
             {QStringLiteral("known"), policy.manifestBindingKnown},
             {QStringLiteral("bound"), policy.manifestBound},
             {QStringLiteral("identity"), policy.manifestIdentity}
         }}
    };
}

} // namespace

QString compoundRuntimeDisplayStateCode(CompoundRuntimeDisplayState state)
{
    switch (state) {
    case CompoundRuntimeDisplayState::AwaitingAuthoritativeContract:
        return QStringLiteral("AWAITING_ARM_CONTRACT");
    case CompoundRuntimeDisplayState::Unavailable:
        return QStringLiteral("UNAVAILABLE");
    case CompoundRuntimeDisplayState::InvalidSource:
        return QStringLiteral("INVALID_SOURCE");
    case CompoundRuntimeDisplayState::UnsupportedVersion:
        return QStringLiteral("UNSUPPORTED_VERSION");
    case CompoundRuntimeDisplayState::Incomplete:
        return QStringLiteral("INCOMPLETE");
    case CompoundRuntimeDisplayState::FreshnessPolicyNotReady:
        return QStringLiteral("FRESHNESS_POLICY_NOT_READY");
    case CompoundRuntimeDisplayState::Stale:
        return QStringLiteral("STALE");
    case CompoundRuntimeDisplayState::GenerationMismatch:
        return QStringLiteral("GENERATION_MISMATCH");
    case CompoundRuntimeDisplayState::LinkDisconnected:
        return QStringLiteral("LINK_DISCONNECTED");
    case CompoundRuntimeDisplayState::Faulted:
        return QStringLiteral("FAULTED");
    case CompoundRuntimeDisplayState::NotOperational:
        return QStringLiteral("NOT_OPERATIONAL");
    case CompoundRuntimeDisplayState::Operational:
        return QStringLiteral("OPERATIONAL");
    }
    return QStringLiteral("INVALID_MODEL_STATE");
}

CompoundRuntimePresentation presentCompoundRuntimeStatus(
    const CompoundRuntimeSnapshot &snapshot)
{
    CompoundRuntimePresentation result;
    result.components = {
        componentPresentation(QStringLiteral("acquisitiond"),
                              QStringLiteral("采集进程 acquisitiond"),
                              snapshot.acquisitiond, false),
        componentPresentation(QStringLiteral("measurementd"),
                              QStringLiteral("测量进程 measurementd"),
                              snapshot.measurementd, false),
        componentPresentation(QStringLiteral("cm_link"),
                              QStringLiteral("C↔M 链路"),
                              snapshot.cmLink, true)
    };

    if (!snapshot.authoritativeContractBound) {
        result.state =
            CompoundRuntimeDisplayState::AwaitingAuthoritativeContract;
        result.stateText = QStringLiteral("等待 ARM 权威复合运行状态合同");
        result.detailText = QStringLiteral(
            "当前输入尚未绑定已冻结的CRS1合同；旧URS2不改义、不替代复合状态。");
        return result;
    }
    if (!snapshot.available) {
        result.state = CompoundRuntimeDisplayState::Unavailable;
        result.stateText = QStringLiteral("复合运行状态不可用");
        result.detailText = snapshot.message.isEmpty()
            ? QStringLiteral("USB在线状态与复合运行状态可用性必须分开。")
            : snapshot.message;
        return result;
    }
    if (!snapshot.sourceBytesValidated) {
        result.state = CompoundRuntimeDisplayState::InvalidSource;
        result.stateText = QStringLiteral("复合运行状态来源未通过权威字节校验");
        result.detailText = QStringLiteral(
            "不得使用未校验对象、缓存对象或旧URS2替代复合状态。");
        return result;
    }
    if (!snapshot.contractVersionKnown
        || !snapshot.contractVersionSupported) {
        result.state = CompoundRuntimeDisplayState::UnsupportedVersion;
        result.stateText = QStringLiteral("复合运行状态版本未知或不受支持");
        result.detailText = snapshot.contractVersionKnown
            ? QStringLiteral("contract=%1；按未知版本拒绝显示健康。")
                  .arg(snapshot.contractVersion)
            : QStringLiteral("未取得可识别的权威合同版本。");
        return result;
    }

    const bool snapshotComplete =
        hasValue(snapshot.snapshotGenerationAvailable,
                    snapshot.snapshotGeneration)
        && hasValue(snapshot.configurationGenerationAvailable,
                    snapshot.configurationGeneration)
        && hasValue(snapshot.systemPackageIdentityAvailable,
                    snapshot.systemPackageSha256)
        && hasValue(snapshot.configurationIdentityAvailable,
                    snapshot.configurationSha256)
        && snapshot.configurationBindingKnown
        && snapshot.generationConsistencyKnown
        && snapshot.runtimeChainClaimKnown;
    if (!snapshotComplete
        || !result.components.at(0).complete
        || !result.components.at(1).complete
        || !result.components.at(2).complete) {
        result.state = CompoundRuntimeDisplayState::Incomplete;
        result.stateText = QStringLiteral("复合运行状态不完整");
        QStringList missing;
        if (!snapshotComplete) missing << QStringLiteral("snapshot");
        for (const CompoundRuntimeComponentPresentation &component :
             result.components) {
            if (!component.complete) missing << component.key;
        }
        result.detailText = QStringLiteral("缺失或未知域：%1。")
                                .arg(missing.join(QStringLiteral(", ")));
        return result;
    }
    const CompoundRuntimeFreshnessPolicyStatus &policy =
        snapshot.freshnessPolicy;
    if (!policyComplete(policy) || !policy.approved
        || !policy.configurationBound || !policy.manifestBound) {
        result.state =
            CompoundRuntimeDisplayState::FreshnessPolicyNotReady;
        result.stateText = QStringLiteral("复合状态freshness policy未批准或未完成绑定");
        result.detailText = QStringLiteral(
            "诊断字段可显示，但缺少allowlist批准、配置绑定或manifest绑定时，RUNTIME_CHAIN_READY=false。");
        return result;
    }
    if ((snapshot.snapshotAgeKnown && !snapshot.snapshotFresh)
        || !snapshot.acquisitiond.fresh
        || !snapshot.measurementd.fresh || !snapshot.cmLink.fresh) {
        result.state = CompoundRuntimeDisplayState::Stale;
        result.stateText = QStringLiteral("复合运行状态部分或整体陈旧");
        result.detailText = QStringLiteral(
            "任一子状态或同快照age陈旧时，不复用其他新鲜字段拼成健康。");
        return result;
    }
    if (!snapshot.generationConsistent) {
        result.state = CompoundRuntimeDisplayState::GenerationMismatch;
        result.stateText = QStringLiteral("复合运行状态代次不一致");
        result.detailText = QStringLiteral(
            "代次关系由ARM合同validator判定；Windows不自行推断等式。");
        return result;
    }
    if (!snapshot.cmLink.connected) {
        result.state = CompoundRuntimeDisplayState::LinkDisconnected;
        result.stateText = QStringLiteral("C↔M 链路断开");
        result.detailText = QStringLiteral(
            "两个daemon即使分别存活，也不得冒充端到端运行链健康。");
        return result;
    }
    if (snapshot.acquisitiond.faultActive
        || snapshot.measurementd.faultActive
        || snapshot.cmLink.faultActive) {
        result.state = CompoundRuntimeDisplayState::Faulted;
        result.stateText = QStringLiteral("复合运行状态存在明确故障");
        result.detailText = QStringLiteral(
            "acquisitiond、measurementd或C↔M链路任一故障均撤销运行链健康。");
        return result;
    }
    if (!snapshot.acquisitiond.stateHealthy
        || !snapshot.measurementd.stateHealthy
        || !snapshot.cmLink.stateHealthy
        || !snapshot.acquisitiond.identityBound
        || !snapshot.measurementd.identityBound
        || !snapshot.cmLink.identityBound
        || !snapshot.configurationBound
        || !snapshot.runtimeChainClaim) {
        result.state = CompoundRuntimeDisplayState::NotOperational;
        result.stateText = QStringLiteral("复合运行链尚未进入健康运行态");
        result.detailText = QStringLiteral(
            "状态已知但未被权威映射为健康；保持fail-closed。");
        return result;
    }

    result.state = CompoundRuntimeDisplayState::Operational;
    result.runtimeChainReady = true;
    result.stateText = QStringLiteral("采集、测量与C↔M链路状态完整且健康");
    result.detailText = QStringLiteral(
        "仅授予RUNTIME_CHAIN_READY；正式力值仍需DevicePack、PL/formal admission、标定和独立measurement snapshot门禁。");
    return result;
}

QJsonObject compoundRuntimeDiagnosticJson(
    const CompoundRuntimeSnapshot &snapshot,
    const CompoundRuntimePresentation &presentation)
{
    const QString parserStatus = !snapshot.authoritativeContractBound
        ? QStringLiteral("BLOCKED_ARM_SCHEMA")
        : (snapshot.sourceBytesValidated
               ? QStringLiteral("ARM_CONTRACT_VALIDATED")
               : QStringLiteral("SOURCE_BYTES_NOT_VALIDATED"));
    const auto component = [&presentation](const QString &key)
        -> CompoundRuntimeComponentPresentation {
        for (const CompoundRuntimeComponentPresentation &entry :
             presentation.components) {
            if (entry.key == key) return entry;
        }
        return {};
    };

    return {
        {QStringLiteral("diagnostic_schema"),
         QStringLiteral("UCM.WINDOWS.COMPOUND_RUNTIME.DIAGNOSTIC.1")},
        {QStringLiteral("parser_status"), parserStatus},
        {QStringLiteral("legacy_urs2_reinterpreted"), false},
        {QStringLiteral("raw_wire_bytes_included"), false},
        {QStringLiteral("runtime_chain_ready"),
         presentation.runtimeChainReady},
        {QStringLiteral("formal_measurement_credit"),
         presentation.formalMeasurementCredit},
        {QStringLiteral("overall_state"),
         compoundRuntimeDisplayStateCode(presentation.state)},
        {QStringLiteral("state_text"), presentation.stateText},
        {QStringLiteral("detail_text"), presentation.detailText},
        {QStringLiteral("contract"), QJsonObject {
             {QStringLiteral("authoritative_bound"),
              snapshot.authoritativeContractBound},
             {QStringLiteral("version_known"),
              snapshot.contractVersionKnown},
             {QStringLiteral("version_supported"),
              snapshot.contractVersionSupported},
             {QStringLiteral("version"), snapshot.contractVersion},
             {QStringLiteral("source_bytes_validated"),
              snapshot.sourceBytesValidated}
         }},
        {QStringLiteral("freshness_policy"),
         policyJson(snapshot.freshnessPolicy)},
        {QStringLiteral("snapshot"), QJsonObject {
             {QStringLiteral("available"), snapshot.available},
             {QStringLiteral("identity"),
              textField(snapshot.snapshotIdentityAvailable,
                        snapshot.snapshotIdentity)},
              {QStringLiteral("generation"),
               textField(snapshot.snapshotGenerationAvailable,
                         snapshot.snapshotGeneration)},
              {QStringLiteral("configuration_generation"),
               textField(snapshot.configurationGenerationAvailable,
                         snapshot.configurationGeneration)},
              {QStringLiteral("system_package_sha256"),
               textField(snapshot.systemPackageIdentityAvailable,
                         snapshot.systemPackageSha256)},
              {QStringLiteral("configuration_sha256"),
               textField(snapshot.configurationIdentityAvailable,
                         snapshot.configurationSha256)},
              {QStringLiteral("configuration_binding"), QJsonObject {
                   {QStringLiteral("known"),
                    snapshot.configurationBindingKnown},
                   {QStringLiteral("bound"), snapshot.configurationBound}
               }},
             {QStringLiteral("age"), QJsonObject {
                  {QStringLiteral("known"), snapshot.snapshotAgeKnown},
                  {QStringLiteral("fresh"), snapshot.snapshotFresh},
                  {QStringLiteral("display"), snapshot.snapshotAgeText}
              }},
             {QStringLiteral("generation_consistency"), QJsonObject {
                  {QStringLiteral("known"),
                   snapshot.generationConsistencyKnown},
                  {QStringLiteral("consistent"),
                   snapshot.generationConsistent}
              }},
             {QStringLiteral("runtime_chain_claim"), QJsonObject {
                  {QStringLiteral("known"), snapshot.runtimeChainClaimKnown},
                  {QStringLiteral("value"), snapshot.runtimeChainClaim}
              }}
         }},
        {QStringLiteral("acquisitiond"),
         componentJson(snapshot.acquisitiond,
                       component(QStringLiteral("acquisitiond")), false)},
        {QStringLiteral("measurementd"),
         componentJson(snapshot.measurementd,
                       component(QStringLiteral("measurementd")), false)},
        {QStringLiteral("cm_link"),
         componentJson(snapshot.cmLink,
                       component(QStringLiteral("cm_link")), true)}
    };
}

} // namespace ucm
