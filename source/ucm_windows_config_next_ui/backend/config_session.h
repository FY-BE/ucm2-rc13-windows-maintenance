#pragma once

#include "config_transport.h"

#include <QJsonObject>
#include <QStringList>

#include <memory>

namespace ucm {

enum class ApplyPhase {
    Editing,
    Prepared,
    ValidationPassed,
    RamCommitted,
    Confirmed,
    Failed
};

QString phaseLabel(ApplyPhase phase, bool failurePreservesActive = true);

enum class ConnectionMaintenanceState {
    NotApplicable,
    Deferred,
    Healthy,
    Reconnected,
    Failed
};

struct ConnectionMaintenanceResult {
    ConnectionMaintenanceState state =
        ConnectionMaintenanceState::NotApplicable;
    QString message;
};

class ConfigurationSession {
public:
    static constexpr int kMaximumHistoryEntries = 512;

    ConfigurationSession();
    explicit ConfigurationSession(std::unique_ptr<ConfigurationTransport> transport);

    const Configuration &active() const { return m_transport->activeConfiguration(); }
    const Configuration &candidate() const { return m_candidate; }
    ApplyPhase phase() const { return m_phase; }
    const ValidationResult &lastValidation() const { return m_lastValidation; }
    const QStringList &history() const { return m_history; }
    quint64 droppedHistoryEntries() const { return m_droppedHistoryEntries; }
    QString lastMessage() const { return m_lastMessage; }
    bool hasStagedConfiguration() const { return m_transport->hasStagedConfiguration(); }
    TransportInfo transportInfo() const { return m_transport->info(); }

    void setCandidate(const Configuration &candidate);
    void resetCycle();
    bool prepare();
    bool validateCandidate();
    bool commitRam();
    bool confirmReadback();
    void injectReadbackFailure();
    ProductReadState readProductState();
    bool productOperationsCommissioned() const { return m_transport->productOperationsCommissioned(); }
    ProductOperationResult submitProductConfiguration(const productv9::ConfigWriteRequest &request, bool authorized) { return m_transport->submitProductConfiguration(request, authorized); }
    ProductOperationResult pollProductConfiguration(quint64 requestId) { return m_transport->pollProductConfiguration(requestId); }
    ProductOperationResult beginProductUpgrade(const productv9::UpgradePackage &package, bool authorized) { return m_transport->beginProductUpgrade(package, authorized); }
    ProductOperationResult writeProductUpgradeChunk(const QByteArray &data) { return m_transport->writeProductUpgradeChunk(data); }
    ProductOperationResult finalizeProductUpgrade() { return m_transport->finalizeProductUpgrade(); }
    ProductOperationResult abortProductUpgrade(quint32 reason, bool authorized) { return m_transport->abortProductUpgrade(reason, authorized); }
    ProductOperationResult pollProductUpgrade() { return m_transport->pollProductUpgrade(); }
    ProductOperationResult activateProductUpgrade(bool authorized) { return m_transport->activateProductUpgrade(authorized); }

    WaveformSnapshot readWaveformSnapshot();
    bool supportsWaveformViewport() const;
    WaveformViewportResult readWaveformViewport(
        const WaveformViewportRequest &request);
    TelemetrySnapshot readTelemetrySnapshot();
    DeviceLogListResult readLogSources();
    DeviceLogChunkResult readLogChunk(quint32 sourceId, quint64 offset,
                                      quint32 maximumBytes,
                                      quint64 snapshotId);
    ConfigurationReceiptDetails configurationReceiptDetails() const;
    void disconnectTransport() { m_transport->disconnect(); }
    bool reconnectTransport();
    TransportPingResult pingTransport(const QByteArray &payload);
    ConnectionMaintenanceResult maintainTransportConnection(
        const QByteArray &probePayload);
    BinaryObjectResult readTelemetryObject();
    BinaryObjectResult readWaveformObject();
    BinaryObjectResult readConfigReceiptObject();
    UsbExtendedDiscoveryV2 usbExtendedDiscovery() const;
    BinaryObjectResult readUsbExtendedCapabilitiesObject();
    BinaryObjectResult readUsbParameterCatalogObject();
    UsbRuntimeConfigOperationResultV9 readRuntimeConfigV9(
        quint32 kind, quint64 transactionId = 0U)
    { return m_transport->readRuntimeConfigV9(kind, transactionId); }
    UsbRuntimeConfigOperationResultV9 submitRuntimeConfigV9(
        const UsbRuntimeConfigObjectV9 &object)
    { return m_transport->submitRuntimeConfigV9(object); }
    ControlAuthorityResultV2 readControlAuthorityState();
    ControlAuthorityResultV2 switchControlMode(quint32 requestedMode);
    ControlAuthorityResultV2 renewControlLease();
    ControlAuthorityResultV2 resumeHostControl();
    BinaryObjectResult readControlAuthorityObject();
    RuntimeStatusResultV2 readRuntimeStatus();
    BinaryObjectResult readRuntimeStatusObject();
    UsbRuntimeProgressResultV9 readRuntimeProgressV9()
    { return m_transport->readRuntimeProgressV9(); }
    UsbRuntimeActionResultV9 executeRuntimeActionV9(
        quint32 action, quint64 expectedGeneration)
    { return m_transport->executeRuntimeActionV9(action, expectedGeneration); }
    UsbCompositeRuntimeTrustResultV1 installCompositeRuntimeTrust(
        const UsbCompositeRuntimeTrustInputV1 &input,
        const UsbCompositeRuntimeManifestVerifierV1 &verifier);
    void clearCompositeRuntimeTrust(const QString &reason);
    UsbCompositeRuntimeStatusResultV1 readCompositeRuntimeStatus();
    BinaryObjectResult readCompositeRuntimeStatusObject();
    QJsonObject evidence() const;

private:
    bool fail(const QString &message);
    void record(const QString &message);

    std::unique_ptr<ConfigurationTransport> m_transport;
    Configuration m_candidate;
    ApplyPhase m_phase = ApplyPhase::Editing;
    ValidationResult m_lastValidation;
    QStringList m_history;
    quint64 m_droppedHistoryEntries = 0;
    QString m_lastMessage;
};

} // namespace ucm
