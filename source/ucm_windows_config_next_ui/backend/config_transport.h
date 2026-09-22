#pragma once

#include "config_model.h"
#include "usb_parameter_config_v9.h"
#include "usb_composite_runtime_status_v1.h"
#include "usb_composite_runtime_trust_v1.h"
#include "usb_runtime_progress_v9.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <array>

namespace ucm {

namespace productv9 { struct ConfigWriteRequest; struct UpgradePackage; }
struct ProductOperationResult {
    bool ok = false; // A valid response; inspect outcome for operation success.
    QString outcome = QStringLiteral("unavailable");
    QString message = QStringLiteral("此后端未实现产品操作。");
    QJsonObject fields;
};

struct TransportResult {
    bool success = false;
    QString message;
};

struct ReadbackResult {
    bool success = false;
    Configuration configuration;
    QString message;
};

struct WaveformSnapshot {
    bool success = false;
    QString message;
    quint64 generation = 0;
    quint64 sessionId = 0;
    quint64 sequence = 0;
    quint64 frameCounter = 0;
    quint32 sampleRateHz = 0;
    quint32 windowStart = 0;
    std::array<QVector<qint16>, 4> rodSamples;
};

// Backward-compatible revision-9 message-7 viewport. The sample offset lives
// at request +12; the final 16 bytes remain zero for every request.
struct WaveformViewportRequest {
    quint64 expectedGeneration = 0;
    quint32 sliceOffset = 0;
    quint32 sampleCount = 2048;
};

struct WaveformViewportResult {
    bool success = false;
    bool supported = false;
    QString message;
    quint32 sliceOffset = 0;
    WaveformSnapshot waveform;
};

struct RodTelemetrySnapshot {
    quint32 measurementFlags = 0;
    quint32 saturationCount = 0;
    quint32 forceReason = 0;
    double referenceT0Sample = 0.0;
    double currentT0Sample = 0.0;
    double nccPeak = 0.0;
    double nccPeakRatio = 0.0;
    double nccLagSamples = 0.0;
    double delayNs = 0.0;
    double snrDb = 0.0;
    double forceN = 0.0;
    double strainMicrostrain = 0.0;
};

struct TelemetrySnapshot {
    bool success = false;
    QString message;
    qint64 observedUtcMs = 0;
    qint64 observedMonotonicNs = 0;
    quint64 generation = 0;
    quint64 publishedMonotonicNs = 0;
    quint64 sessionId = 0;
    quint64 sequence = 0;
    quint64 frameCounter = 0;
    quint32 flags = 0;
    quint32 profile = 0;
    quint32 windowStart = 0;
    quint32 windowLength = 0;
    quint32 measurementValidMask = 0;
    quint32 forceAvailableMask = 0;
    quint32 rodValidMask = 0;
    quint32 strainAvailableMask = 0;
    quint32 negativeForceMask = 0;
    quint32 processState = 0;
    bool processStateTrusted = false;
    quint16 producerStatusFlags = 0;
    quint16 producerStatusCoverage = 0;
    quint16 combinedStatusFlags = 0;
    quint16 combinedStatusCoverage = 0;
    quint32 healthState = 0;
    quint32 primaryReasonCode = 0;
    quint32 afeProfileCrc32 = 0;
    quint32 afeReceiptSequence = 0;
    quint32 trainingReceiptSequence = 0;
    quint32 cadenceReceiptSequence = 0;
    quint32 hvSetpoint = 0;
    quint32 hvReceiptSequence = 0;
    quint32 digitalTgcAttenuationDb = 0;
    bool formalForceValid = false;
    bool lowLoadBiasInvalid = false;
    quint32 biasValidMinTotalForceN = 0;
    quint32 forcePredictionMask = 0;
    quint32 forceQualityDegradedMask = 0;
    quint32 forceCommonTrendOutlierMask = 0;
    quint32 plcState = 0;
    bool plcStale = false;
    quint32 agcState = 0;
    quint32 agcReason = 0;
    quint32 actualPgaDb = 0;
    std::array<quint32, 4> actualLnaDb {};
    quint32 actualVcntlDac = 0;
    quint32 actualHvVolts = 0;
    quint32 actualBurstCycles = 0;
    double formalTotalN = 0.0;
    double imbalanceIndex = 0.0;
    double k0NsPerN = 0.0;
    std::array<RodTelemetrySnapshot, 4> rod {};
    bool productResult = false;
    bool diagnosticFieldsAvailable = true;
    quint64 captureRequestId = 0;
    QJsonObject pairedInputStatus;
};

struct DeviceLogSource {
    quint32 sourceId = 0;
    bool available = false;
    quint64 totalBytes = 0;
    quint64 modifiedTimeNs = 0;
    quint64 snapshotId = 0;
    quint32 fileIdentityCrc32 = 0;
    QString name;
    bool mutableFile = false;
    quint32 kind = 1;
};

struct DeviceLogListResult {
    bool success = false;
    QString message;
    QVector<DeviceLogSource> sources;
};

struct DeviceLogChunkResult {
    bool success = false;
    QString message;
    quint32 sourceId = 0;
    quint64 offset = 0;
    quint64 totalBytes = 0;
    quint32 fileIdentityCrc32 = 0;
    quint64 snapshotId = 0;
    bool more = false;
    QByteArray data;
};

struct HardwareConfigReceipt {
    bool valid = false;
    quint32 stateKind = 0;
    qint32 result = 0;
    quint32 digitalTgcAttenuationDb = 0;
    bool persisted = false;
    quint64 transactionId = 0;
    quint64 baseGeneration = 0;
    quint64 requestedGeneration = 0;
    quint64 activeGeneration = 0;
    quint64 frontendSessionId = 0;
    quint64 frontendSessionGeneration = 0;
    quint32 requestCrc32 = 0;
    quint32 afeProfileCrc32 = 0;
    quint32 afeReceiptSequence = 0;
    quint32 trainingReceiptSequence = 0;
    quint32 cadenceReceiptSequence = 0;
};

struct ConfigurationReceiptDetails {
    bool available = false;
    quint32 kind = 0;
    qint32 result = 0;
    bool persisted = false;
    quint64 transactionId = 0;
    quint64 baseGeneration = 0;
    quint64 requestedGeneration = 0;
    quint64 activeGeneration = 0;
    quint64 changedGroupMask = 0;
    QByteArray activeConfigurationSha256;
    std::array<qint32, 13> fieldResults {};
    HardwareConfigReceipt hardware;
};

struct TransportPingResult {
    bool success = false;
    QString message;
    quint32 payloadBytes = 0;
    qint64 roundTripMicroseconds = 0;
};

struct BinaryObjectResult {
    bool success = false;
    QString message;
    QByteArray data;
};

struct ProductReadState {
    bool supported = false;
    bool success = false;
    QString message;
    QJsonObject deviceModel;
    QJsonObject inputPolicy;
    QJsonObject pairedInputStatus;
};

struct UsbExtendedCapabilitiesV2 {
    quint16 schemaVersion = 0;
    quint32 flags = 0;
    quint32 protocolRevision = 0;
    quint32 maximumPayloadBytes = 0;
    quint32 recommendedChunkBytes = 0;
    quint32 maximumOutstandingRequests = 0;
    quint32 configStructBytes = 0;
    quint32 configReceiptBytes = 0;
    quint32 parameterCatalogEntries = 0;
    quint64 supportedConfigGroupMask = 0;
    quint64 activeConfigGroupMask = 0;
    quint64 supportedUpgradeTargetMask = 0;
    quint64 activeUpgradeTargetMask = 0;
    quint64 supportedFeatureMask = 0;
    quint64 activeFeatureMask = 0;
    quint32 afeScopeFlags = 0;
    quint32 parameterCatalogCrc32 = 0;
    QString deviceClass;
    QString buildId;
    quint32 authorityStateBytes = 0;
    quint32 appliedControlMode = 0;
    quint32 controlPhase = 0;
    quint64 authorityGeneration = 0;
    quint32 deviceModelControlAbi = 0;
    quint32 deviceModelRequestHeaderBytes = 0;
    quint32 deviceModelStateBytes = 0;
    quint32 deviceModelDocumentHeaderBytes = 0;
    quint32 deviceModelJsonMaxBytes = 0;
    quint32 deviceModelOperationMask = 0;
    quint32 resultSnapshotAbi = 0;
    quint32 resultSnapshotBytes = 0;
    quint32 logCatalogAbi = 0;
    quint32 logEntryBytes = 0;
    quint32 logEntriesMax = 0;
    quint32 logChunkMax = 0;
    quint32 systemInputPolicyControlAbi = 0;
    quint32 systemInputPolicyRequestHeaderBytes = 0;
    quint32 systemInputPolicyStateBytes = 0;
    quint32 systemInputPolicyDocumentHeaderBytes = 0;
    quint32 systemInputPolicyJsonMaxBytes = 0;
    quint32 systemInputPolicyOperationMask = 0;
};

struct UsbParameterDescriptorV2 {
    quint32 fieldId = 0;
    quint16 groupId = 0;
    quint8 valueKind = 0;
    quint8 scope = 0;
    quint32 accessFlags = 0;
    quint32 constraintFlags = 0;
    double minimumValue = 0.0;
    double maximumValue = 0.0;
    double stepValue = 0.0;
    quint64 enumMask = 0;
};

struct UsbExtendedDiscoveryV2 {
    bool available = false;
    bool catalogReady = false;
    QString message = QStringLiteral("尚未探测 USB V2 后端。");
    UsbExtendedCapabilitiesV2 capabilities;
    QVector<UsbParameterDescriptorV2> parameters;
};

struct ControlAuthorityStateV2 {
    bool available = false;
    quint32 appliedMode = 0;
    quint32 requestedMode = 0;
    quint32 phase = 0;
    quint32 owner = 0;
    quint32 transitionReason = 0;
    bool hardwareActive = false;
    qint32 lastResult = 0;
    quint64 generation = 0;
    quint64 transactionId = 0;
    quint64 lastTransactionId = 0;
    quint64 hostLeaseDeadlineNs = 0;
    quint64 publishedMonotonicNs = 0;
};

struct ControlAuthorityResultV2 {
    bool success = false;
    QString message;
    ControlAuthorityStateV2 state;
    quint64 transactionId = 0;
};

struct RuntimeStatusV2 {
    bool available = false;
    quint32 flags = 0;
    quint64 daemonInstanceId = 0;
    quint64 heartbeatSequence = 0;
    quint64 startedMonotonicNs = 0;
    quint64 publishedMonotonicNs = 0;
    quint64 activeConfigGeneration = 0;
    quint64 activeSessionId = 0;
    quint64 lastCaptureSequence = 0;
    quint64 lastFrameCounter = 0;
    quint64 lastCaptureMonotonicNs = 0;
    quint64 lastCanonicalMonotonicNs = 0;
    quint64 formalSinkDropCount = 0;
    quint64 runtimeStatusDropCount = 0;
    quint64 recoveryCount = 0;
    quint32 runState = 0;
    quint32 controlMode = 0;
    quint32 controlPhase = 0;
    quint32 controlOwner = 0;
    bool hardwareActive = false;
    quint32 canonicalPhase = 0;
    quint32 templateState = 0;
    quint32 formalReason = 0;
    quint32 lastCycleStatus = 0;
    quint32 lastFaultDomain = 0;
    qint32 lastFaultCode = 0;
    quint32 formalSinkState = 0;
    quint32 displaySinkState = 0;
    quint32 usbTelemetrySinkState = 0;
    quint32 usbWaveformSinkState = 0;
    quint32 measurementValidMask = 0;
    quint32 formalRodValidMask = 0;
    quint32 maximumAgeMs = 0;
    quint32 buildIdentityCrc32 = 0;
    quint32 activeContextCrc32 = 0;
};

struct RuntimeStatusResultV2 {
    bool success = false;
    QString message;
    RuntimeStatusV2 status;
};

struct TransportInfo {
    QString name;
    bool simulationOnly = true;
    bool realUsbOpened = false;
    bool com7Opened = false;
    bool armReceiverContacted = false;
    bool nonvolatileWrite = false;
    bool supportsFailureInjection = false;
    QString scope = QStringLiteral("full_configuration");
    bool hardwareReceiptValid = false;
    bool failurePreservesActive = true;
};

class ConfigurationTransport {
public:
    virtual ~ConfigurationTransport() = default;

    virtual const Configuration &activeConfiguration() const = 0;
    virtual bool hasStagedConfiguration() const = 0;
    virtual TransportInfo info() const = 0;
    virtual QJsonObject evidence() const { return {}; }
    virtual ProductReadState readProductState() { return {}; }
    virtual bool productOperationsCommissioned() const { return false; }
    virtual ProductOperationResult submitProductConfiguration(const productv9::ConfigWriteRequest &request, bool authorized) { return {}; }
    virtual ProductOperationResult pollProductConfiguration(quint64 requestId) { return {}; }
    virtual ProductOperationResult beginProductUpgrade(const productv9::UpgradePackage &package, bool authorized) { return {}; }
    virtual ProductOperationResult writeProductUpgradeChunk(const QByteArray &data) { return {}; }
    virtual ProductOperationResult finalizeProductUpgrade() { return {}; }
    virtual ProductOperationResult abortProductUpgrade(quint32 reason, bool authorized) { return {}; }
    virtual ProductOperationResult pollProductUpgrade() { return {}; }
    virtual ProductOperationResult activateProductUpgrade(bool authorized) { return {}; }

    virtual void disconnect() {}

    virtual TransportResult stageRam(const Configuration &candidate) = 0;
    virtual ReadbackResult readBackStaged() = 0;
    virtual TransportResult confirmReadback() = 0;
    virtual void rollbackStagedRam() = 0;
    virtual bool injectNextReadbackFailure() { return false; }
    virtual WaveformSnapshot readWaveformSnapshot()
    {
        return {false, QStringLiteral("当前 transport 不提供实时波形。")};
    }
    virtual bool supportsWaveformViewport() const { return false; }
    virtual WaveformViewportResult readWaveformViewport(
        const WaveformViewportRequest &)
    {
        return {false, false, QStringLiteral(
            "当前 transport 不支持消息7波形切片。")};
    }
    virtual TelemetrySnapshot readTelemetrySnapshot()
    {
        return {false, QStringLiteral("当前 transport 不提供实时中间量。")};
    }
    virtual DeviceLogListResult readLogSources()
    {
        return {false, QStringLiteral("当前 transport 不提供设备日志。"), {}};
    }
    virtual DeviceLogChunkResult readLogChunk(
        quint32, quint64, quint32, quint64)
    {
        return {false, QStringLiteral("当前 transport 不提供设备日志。")};
    }
    virtual ConfigurationReceiptDetails configurationReceiptDetails() const
    {
        return {};
    }
    virtual TransportResult reconnect()
    {
        return {false, QStringLiteral("当前 transport 不支持手动重连。")};
    }
    virtual TransportPingResult ping(const QByteArray &)
    {
        return {false, QStringLiteral("当前 transport 不支持链路Ping。")};
    }
    virtual BinaryObjectResult readTelemetryObject()
    {
        return {false, QStringLiteral("当前 transport 不提供原始遥测对象。")};
    }
    virtual BinaryObjectResult readWaveformObject()
    {
        return {false, QStringLiteral("当前 transport 不提供原始波形对象。")};
    }
    virtual BinaryObjectResult readConfigReceiptObject()
    {
        return {false, QStringLiteral("当前 transport 不提供原始配置回执。")};
    }
    virtual UsbExtendedDiscoveryV2 usbExtendedDiscovery() const
    {
        return {};
    }
    virtual BinaryObjectResult readUsbExtendedCapabilitiesObject()
    {
        return {false, QStringLiteral("当前 transport 不提供 USB V2 能力对象。")};
    }
    virtual BinaryObjectResult readUsbParameterCatalogObject()
    {
        return {false, QStringLiteral("当前 transport 不提供 USB V2 参数目录。")};
    }
    virtual UsbRuntimeConfigOperationResultV9 readRuntimeConfigV9(
        quint32, quint64 = 0U)
    {
        return {false, QStringLiteral("当前 transport 不提供 revision 9运行配置状态。"), {}};
    }
    virtual UsbRuntimeConfigOperationResultV9 submitRuntimeConfigV9(
        const UsbRuntimeConfigObjectV9 &)
    {
        return {false, QStringLiteral("当前 transport 不支持 revision 9运行配置写入。"), {}};
    }
    virtual ControlAuthorityResultV2 readControlAuthorityState()
    {
        return {false, QStringLiteral("当前 transport 不提供 USB V2 控制权状态。")};
    }
    virtual ControlAuthorityResultV2 switchControlMode(quint32)
    {
        return {false, QStringLiteral("当前 transport 不支持切换设备控制模式。")};
    }
    virtual ControlAuthorityResultV2 renewControlLease()
    {
        return {false, QStringLiteral("当前 transport 不支持续租 USB 手动控制。")};
    }
    virtual ControlAuthorityResultV2 resumeHostControl()
    {
        return {false, QStringLiteral("当前 transport 不支持恢复 USB 手动控制。")};
    }
    virtual BinaryObjectResult readControlAuthorityObject()
    {
        return {false, QStringLiteral("当前 transport 不提供控制权原始对象。")};
    }
    virtual RuntimeStatusResultV2 readRuntimeStatus()
    {
        return {false, QStringLiteral("当前 transport 不提供 ARM 运行状态。")};
    }
    virtual BinaryObjectResult readRuntimeStatusObject()
    {
        return {false, QStringLiteral("当前 transport 不提供 ARM 运行状态原始对象。")};
    }
    virtual UsbRuntimeProgressResultV9 readRuntimeProgressV9()
    {
        return {false, QStringLiteral("当前 transport 不提供 ARM 运行进度。")};
    }
    virtual UsbRuntimeActionResultV9 executeRuntimeActionV9(
        quint32, quint64)
    {
        return {false, QStringLiteral("当前 transport 不支持 ARM 运行动作。")};
    }
    virtual UsbCompositeRuntimeStatusResultV1 readCompositeRuntimeStatus()
    {
        UsbCompositeRuntimeStatusResultV1 result;
        result.message = QStringLiteral(
            "当前 transport 不提供 ARM CRS1复合运行状态。");
        result.snapshot.authoritativeContractBound = true;
        return result;
    }
    virtual UsbCompositeRuntimeTrustResultV1 installCompositeRuntimeTrust(
        const UsbCompositeRuntimeTrustInputV1 &,
        const UsbCompositeRuntimeManifestVerifierV1 &)
    {
        UsbCompositeRuntimeTrustSourceV1 unavailable;
        return {false,
                QStringLiteral("当前 transport 不支持注入 CRS1 独立信任对象。"),
                UsbCompositeRuntimeTrustCodeV1::NotConfigured,
                unavailable.evidence()};
    }
    virtual void clearCompositeRuntimeTrust(const QString &) {}
    virtual BinaryObjectResult readCompositeRuntimeStatusObject()
    {
        return {false,
                QStringLiteral("当前 transport 不提供 ARM CRS1原始对象。")};
    }
};

} // namespace ucm
