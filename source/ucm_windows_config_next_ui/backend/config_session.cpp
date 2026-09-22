#include "config_session.h"
#include "mock_transport.h"

#include <QDateTime>
#include <QJsonArray>

namespace ucm {

QString phaseLabel(ApplyPhase phase, bool failurePreservesActive)
{
    switch (phase) {
    case ApplyPhase::Editing: return QStringLiteral("编辑配置");
    case ApplyPhase::Prepared: return QStringLiteral("准备完成");
    case ApplyPhase::ValidationPassed: return QStringLiteral("校验通过");
    case ApplyPhase::RamCommitted: return QStringLiteral("V2事务已受理，等待终态");
    case ApplyPhase::Confirmed: return QStringLiteral("回读已确认");
    case ApplyPhase::Failed: return failurePreservesActive
        ? QStringLiteral("失败，旧配置保持")
        : QStringLiteral("失败，设备状态未知");
    }
    return QStringLiteral("未知状态");
}

ConfigurationSession::ConfigurationSession()
    : ConfigurationSession(std::make_unique<MockTransport>(Configuration {}))
{
}

ConfigurationSession::ConfigurationSession(
    std::unique_ptr<ConfigurationTransport> transport)
    : m_transport(std::move(transport))
    , m_candidate(m_transport ? m_transport->activeConfiguration() : Configuration {})
{
    if (!m_transport) {
        m_transport = std::make_unique<MockTransport>(Configuration {});
        m_candidate = m_transport->activeConfiguration();
    }
    const TransportInfo info = m_transport->info();
    record(QStringLiteral("配置会话已创建；transport=%1，scope=%2，simulation_only=%3，USB=%4，COM7=%5，ARM_receiver=%6。")
               .arg(info.name, info.scope,
                    info.simulationOnly ? QStringLiteral("true") : QStringLiteral("false"),
                    info.realUsbOpened ? QStringLiteral("true") : QStringLiteral("false"),
                    info.com7Opened ? QStringLiteral("true") : QStringLiteral("false"),
                    info.armReceiverContacted ? QStringLiteral("true") : QStringLiteral("false")));
}

void ConfigurationSession::setCandidate(const Configuration &candidate)
{
    const bool discardedStagedConfiguration = m_transport->hasStagedConfiguration();
    if (discardedStagedConfiguration) {
        m_transport->rollbackStagedRam();
    }
    m_candidate = candidate;
    if (m_phase != ApplyPhase::Editing) {
        m_phase = ApplyPhase::Editing;
        m_lastMessage = discardedStagedConfiguration
            ? QStringLiteral("候选配置已修改；易失暂存对象已撤销，生命周期回到“编辑配置”。")
            : QStringLiteral("候选配置已修改；生命周期回到“编辑配置”。");
        record(m_lastMessage);
    }
}

void ConfigurationSession::resetCycle()
{
    m_transport->rollbackStagedRam();
    m_phase = ApplyPhase::Editing;
    m_lastValidation = {};
    m_lastMessage = QStringLiteral("已复位到编辑阶段；活动配置未变。" );
    record(m_lastMessage);
}

bool ConfigurationSession::prepare()
{
    if (m_phase != ApplyPhase::Editing && m_phase != ApplyPhase::Failed && m_phase != ApplyPhase::Confirmed) {
        return fail(QStringLiteral("必须先完成或复位当前流程，才能再次准备。"));
    }
    m_transport->rollbackStagedRam();
    const QString version = nextConfigVersion(active().cfgVersion);
    if (version.isEmpty()) {
        return fail(QStringLiteral("活动 cfg_version 无法安全递增。"));
    }
    m_candidate.cfgVersion = version;
    m_phase = ApplyPhase::Prepared;
    m_lastMessage = QStringLiteral("已准备完整配置对象；base_cfg_version=%1，candidate_cfg_version=%2。")
                        .arg(active().cfgVersion, m_candidate.cfgVersion);
    record(m_lastMessage);
    return true;
}

bool ConfigurationSession::validateCandidate()
{
    if (m_phase != ApplyPhase::Prepared) {
        return fail(QStringLiteral("校验只能在“准备完成”后执行。"));
    }
    m_lastValidation = validate(m_candidate);
    if (!m_lastValidation.isValid()) {
        return fail(QStringLiteral("离线校验失败：\n%1").arg(m_lastValidation.summary()));
    }
    const ConfigIdentity id = identityFor(m_candidate);
    m_phase = ApplyPhase::ValidationPassed;
    m_lastMessage = QStringLiteral("校验通过：schema、范围、映射、CRC=%1、SHA256=%2。")
                        .arg(id.crc32, id.sha256.left(16) + QStringLiteral("…"));
    record(m_lastMessage);
    return true;
}

bool ConfigurationSession::commitRam()
{
    if (m_phase != ApplyPhase::ValidationPassed) {
        return fail(QStringLiteral("仅通过校验的完整配置对象可以提交至易失暂存。"));
    }
    const TransportResult result = m_transport->stageRam(m_candidate);
    if (!result.success) {
        return fail(QStringLiteral("易失配置暂存失败：%1").arg(result.message));
    }
    m_phase = ApplyPhase::RamCommitted;
    m_lastMessage = result.message;
    record(m_lastMessage);
    return true;
}

bool ConfigurationSession::confirmReadback()
{
    if (m_phase != ApplyPhase::RamCommitted) {
        return fail(QStringLiteral("回读确认只能在易失暂存成功后执行。"));
    }
    const ReadbackResult readback = m_transport->readBackStaged();
    if (!readback.success) {
        return fail(QStringLiteral("回读失败：%1").arg(readback.message));
    }
    if (identityFor(readback.configuration).sha256 != identityFor(m_candidate).sha256) {
        return fail(QStringLiteral("回读 SHA256 与候选配置不一致；已撤销暂存对象。"));
    }
    const TransportResult result = m_transport->confirmReadback();
    if (!result.success) {
        return fail(QStringLiteral("确认失败：%1").arg(result.message));
    }
    m_phase = ApplyPhase::Confirmed;
    m_lastMessage = result.message;
    record(m_lastMessage);
    return true;
}

void ConfigurationSession::injectReadbackFailure()
{
    if (!m_transport->injectNextReadbackFailure()) {
        m_lastMessage = QStringLiteral("当前 transport 不支持回读失败注入。" );
        record(m_lastMessage);
        return;
    }
    m_lastMessage = QStringLiteral("下一个回读将失败，用于验证旧配置保持策略。" );
    record(m_lastMessage);
}

WaveformSnapshot ConfigurationSession::readWaveformSnapshot()
{
    return m_transport->readWaveformSnapshot();
}

bool ConfigurationSession::supportsWaveformViewport() const
{
    return m_transport->supportsWaveformViewport();
}

WaveformViewportResult ConfigurationSession::readWaveformViewport(
    const WaveformViewportRequest &request)
{
    return m_transport->readWaveformViewport(request);
}

TelemetrySnapshot ConfigurationSession::readTelemetrySnapshot()
{
    return m_transport->readTelemetrySnapshot();
}

DeviceLogListResult ConfigurationSession::readLogSources()
{
    return m_transport->readLogSources();
}

DeviceLogChunkResult ConfigurationSession::readLogChunk(
    quint32 sourceId, quint64 offset, quint32 maximumBytes,
    quint64 snapshotId)
{
    return m_transport->readLogChunk(
        sourceId, offset, maximumBytes, snapshotId);
}

ConfigurationReceiptDetails ConfigurationSession::configurationReceiptDetails() const
{
    return m_transport->configurationReceiptDetails();
}

bool ConfigurationSession::reconnectTransport()
{
    m_transport->rollbackStagedRam();
    const TransportResult result = m_transport->reconnect();
    m_phase = ApplyPhase::Editing;
    m_lastValidation = {};
    if (!result.success) {
        m_lastMessage = QStringLiteral("设备重连失败：%1").arg(result.message);
        record(m_lastMessage);
        return false;
    }
    m_candidate = m_transport->activeConfiguration();
    m_lastMessage = result.message;
    record(m_lastMessage);
    return true;
}

TransportPingResult ConfigurationSession::pingTransport(
    const QByteArray &payload)
{
    const TransportPingResult result = m_transport->ping(payload);
    record(result.message);
    return result;
}

ConnectionMaintenanceResult ConfigurationSession::maintainTransportConnection(
    const QByteArray &probePayload)
{
    const TransportInfo transport = m_transport->info();
    if (transport.simulationOnly
        || transport.scope != QStringLiteral("full_whitelisted_configuration")) {
        return {ConnectionMaintenanceState::NotApplicable,
                QStringLiteral("当前transport不是真实WinUSB。")};
    }
    const bool phaseSafe = m_phase == ApplyPhase::Editing
        || m_phase == ApplyPhase::Confirmed || m_phase == ApplyPhase::Failed;
    if (!phaseSafe || m_transport->hasStagedConfiguration()
        || !diff(m_transport->activeConfiguration(), m_candidate).isEmpty()) {
        return {ConnectionMaintenanceState::Deferred,
                QStringLiteral("存在未确认候选或正在下发，自动重连已暂停。")};
    }

    QString healthFailure;
    if (transport.realUsbOpened && transport.armReceiverContacted) {
        const TransportPingResult probe = m_transport->ping(probePayload);
        if (probe.success) {
            return {ConnectionMaintenanceState::Healthy,
                    QStringLiteral("自动健康检查PASS：%1 B，%2 μs。")
                        .arg(probe.payloadBytes)
                        .arg(probe.roundTripMicroseconds)};
        }
        healthFailure = probe.message;
    }

    const bool reconnected = reconnectTransport();
    if (reconnected) {
        return {ConnectionMaintenanceState::Reconnected,
                healthFailure.isEmpty()
                    ? QStringLiteral("检测到USB未就绪，已自动重连并重读活动配置。")
                    : QStringLiteral("健康检查失败（%1），已自动重连并重读活动配置。")
                          .arg(healthFailure)};
    }
    return {ConnectionMaintenanceState::Failed,
            healthFailure.isEmpty()
                ? m_lastMessage
                : QStringLiteral("健康检查失败（%1）；%2")
                      .arg(healthFailure, m_lastMessage)};
}

BinaryObjectResult ConfigurationSession::readTelemetryObject()
{
    return m_transport->readTelemetryObject();
}

BinaryObjectResult ConfigurationSession::readWaveformObject()
{
    return m_transport->readWaveformObject();
}

BinaryObjectResult ConfigurationSession::readConfigReceiptObject()
{
    return m_transport->readConfigReceiptObject();
}

UsbExtendedDiscoveryV2 ConfigurationSession::usbExtendedDiscovery() const
{
    return m_transport->usbExtendedDiscovery();
}

BinaryObjectResult ConfigurationSession::readUsbExtendedCapabilitiesObject()
{
    return m_transport->readUsbExtendedCapabilitiesObject();
}

BinaryObjectResult ConfigurationSession::readUsbParameterCatalogObject()
{
    return m_transport->readUsbParameterCatalogObject();
}

ControlAuthorityResultV2 ConfigurationSession::readControlAuthorityState()
{
    return m_transport->readControlAuthorityState();
}

ControlAuthorityResultV2 ConfigurationSession::switchControlMode(
    quint32 requestedMode)
{
    const ControlAuthorityResultV2 result =
        m_transport->switchControlMode(requestedMode);
    record(result.message);
    if (result.success) {
        m_candidate = m_transport->activeConfiguration();
        m_phase = ApplyPhase::Editing;
        m_lastValidation = {};
    }
    return result;
}

ControlAuthorityResultV2 ConfigurationSession::renewControlLease()
{
    const ControlAuthorityResultV2 result = m_transport->renewControlLease();
    record(result.message);
    return result;
}

ControlAuthorityResultV2 ConfigurationSession::resumeHostControl()
{
    const ControlAuthorityResultV2 result = m_transport->resumeHostControl();
    record(result.message);
    if (result.success) {
        m_candidate = m_transport->activeConfiguration();
        m_phase = ApplyPhase::Editing;
        m_lastValidation = {};
    }
    return result;
}

BinaryObjectResult ConfigurationSession::readControlAuthorityObject()
{
    return m_transport->readControlAuthorityObject();
}

ProductReadState ConfigurationSession::readProductState()
{
    return m_transport->readProductState();
}

RuntimeStatusResultV2 ConfigurationSession::readRuntimeStatus()
{
    return m_transport->readRuntimeStatus();
}

BinaryObjectResult ConfigurationSession::readRuntimeStatusObject()
{
    return m_transport->readRuntimeStatusObject();
}

UsbCompositeRuntimeTrustResultV1
ConfigurationSession::installCompositeRuntimeTrust(
    const UsbCompositeRuntimeTrustInputV1 &input,
    const UsbCompositeRuntimeManifestVerifierV1 &verifier)
{
    const UsbCompositeRuntimeTrustResultV1 result =
        m_transport->installCompositeRuntimeTrust(input, verifier);
    record(result.message);
    return result;
}

void ConfigurationSession::clearCompositeRuntimeTrust(
    const QString &reason)
{
    m_transport->clearCompositeRuntimeTrust(reason);
    record(reason.trimmed().isEmpty()
               ? QStringLiteral("CRS1独立信任状态已清除。")
               : reason);
}

UsbCompositeRuntimeStatusResultV1
ConfigurationSession::readCompositeRuntimeStatus()
{
    return m_transport->readCompositeRuntimeStatus();
}

BinaryObjectResult ConfigurationSession::readCompositeRuntimeStatusObject()
{
    return m_transport->readCompositeRuntimeStatusObject();
}

QJsonObject ConfigurationSession::evidence() const
{
    const ConfigIdentity activeIdentity = identityFor(active());
    const ConfigIdentity candidateIdentity = identityFor(candidate());
    const TransportInfo transport = m_transport->info();
    QJsonArray changes;
    for (const QString &entry : diff(active(), candidate())) {
        changes.append(entry);
    }
    QJsonArray events;
    for (const QString &entry : m_history) {
        events.append(entry);
    }
    const UsbExtendedDiscoveryV2 extended =
        m_transport->usbExtendedDiscovery();
    QJsonArray extendedParameters;
    for (const UsbParameterDescriptorV2 &parameter : extended.parameters) {
        extendedParameters.append(QJsonObject {
            {QStringLiteral("field_id"), static_cast<int>(parameter.fieldId)},
            {QStringLiteral("group_id"), static_cast<int>(parameter.groupId)},
            {QStringLiteral("value_kind"), static_cast<int>(parameter.valueKind)},
            {QStringLiteral("scope"), static_cast<int>(parameter.scope)},
            {QStringLiteral("access_flags"),
             QStringLiteral("0x%1").arg(parameter.accessFlags, 0, 16)},
            {QStringLiteral("constraint_flags"),
             QStringLiteral("0x%1").arg(parameter.constraintFlags, 0, 16)},
            {QStringLiteral("minimum"), parameter.minimumValue},
            {QStringLiteral("maximum"), parameter.maximumValue},
            {QStringLiteral("step"), parameter.stepValue},
            {QStringLiteral("enum_mask"),
             QStringLiteral("0x%1").arg(parameter.enumMask, 0, 16)}
        });
    }
    const QJsonObject extendedEvidence {
        {QStringLiteral("available"), extended.available},
        {QStringLiteral("catalog_ready"), extended.catalogReady},
        {QStringLiteral("message"), extended.message},
        {QStringLiteral("schema_version"),
         static_cast<int>(extended.capabilities.schemaVersion)},
        {QStringLiteral("protocol_revision"),
         static_cast<int>(extended.capabilities.protocolRevision)},
        {QStringLiteral("device_class"),
         extended.capabilities.deviceClass},
        {QStringLiteral("build_id"), extended.capabilities.buildId},
        {QStringLiteral("catalog_crc32"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.parameterCatalogCrc32, 8, 16,
             QLatin1Char('0'))},
        {QStringLiteral("supported_feature_mask"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.supportedFeatureMask, 0, 16)},
        {QStringLiteral("active_feature_mask"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.activeFeatureMask, 0, 16)},
        {QStringLiteral("supported_config_group_mask"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.supportedConfigGroupMask, 0, 16)},
        {QStringLiteral("active_config_group_mask"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.activeConfigGroupMask, 0, 16)},
        {QStringLiteral("supported_upgrade_target_mask"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.supportedUpgradeTargetMask, 0, 16)},
        {QStringLiteral("active_upgrade_target_mask"),
         QStringLiteral("0x%1").arg(
             extended.capabilities.activeUpgradeTargetMask, 0, 16)},
        {QStringLiteral("parameters"), extendedParameters}
    };
    return QJsonObject {
        {QStringLiteral("evidence_schema"), QStringLiteral("UCM.CONFIG.EVIDENCE.2")},
        {QStringLiteral("generated_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("simulation_only"), transport.simulationOnly},
        {QStringLiteral("transport"), transport.name},
        {QStringLiteral("real_usb_opened"), transport.realUsbOpened},
        {QStringLiteral("com7_opened"), transport.com7Opened},
        {QStringLiteral("arm_receiver_contacted"), transport.armReceiverContacted},
        {QStringLiteral("nonvolatile_write"), transport.nonvolatileWrite},
        {QStringLiteral("transport_scope"), transport.scope},
        {QStringLiteral("hardware_receipt_valid"), transport.hardwareReceiptValid},
        {QStringLiteral("transport_evidence"), m_transport->evidence()},
        {QStringLiteral("usb_v2"), extendedEvidence},
        {QStringLiteral("staged_configuration_present"), m_transport->hasStagedConfiguration()},
        {QStringLiteral("phase"),
         phaseLabel(m_phase, transport.failurePreservesActive)},
        {QStringLiteral("base_cfg_version"), active().cfgVersion},
        {QStringLiteral("candidate_cfg_version"), candidate().cfgVersion},
        {QStringLiteral("active_configuration"), active().toJson()},
        {QStringLiteral("candidate_configuration"), candidate().toJson()},
        {QStringLiteral("active_identity"), QJsonObject{{QStringLiteral("crc32"), activeIdentity.crc32}, {QStringLiteral("sha256"), activeIdentity.sha256}}},
        {QStringLiteral("candidate_identity"), QJsonObject{{QStringLiteral("crc32"), candidateIdentity.crc32}, {QStringLiteral("sha256"), candidateIdentity.sha256}}},
        {QStringLiteral("change_diff"), changes},
        {QStringLiteral("events_capacity"), kMaximumHistoryEntries},
        {QStringLiteral("events_retained"), m_history.size()},
        {QStringLiteral("events_dropped"),
         static_cast<double>(m_droppedHistoryEntries)},
        {QStringLiteral("events"), events}
    };
}

bool ConfigurationSession::fail(const QString &message)
{
    m_transport->rollbackStagedRam();
    m_phase = ApplyPhase::Failed;
    const TransportInfo info = m_transport->info();
    m_lastMessage = message + (info.failurePreservesActive
        ? QStringLiteral(" 活动配置保持不变。")
        : QStringLiteral(" 未取得有效硬件确认；设备状态按未知处理，必须重新连接查询。"));
    record(m_lastMessage);
    return false;
}

void ConfigurationSession::record(const QString &message)
{
    m_history << QStringLiteral("%1  %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message);
    while (m_history.size() > kMaximumHistoryEntries) {
        m_history.removeFirst();
        ++m_droppedHistoryEntries;
    }
}

} // namespace ucm
