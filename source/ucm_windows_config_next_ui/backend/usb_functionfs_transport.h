#pragma once

#include "config_transport.h"
#include "libusb_backend.h"
#include "usb_extended_wire_v2.h"
#include "usb_wire_v1.h"
#include "usb_product_wire_v9.h"
#include "usb_product_operations_v9.h"

#include <memory>
#include <mutex>
#include <QElapsedTimer>

namespace ucm {

class UsbFunctionfsTransport final : public ConfigurationTransport {
public:
    UsbFunctionfsTransport();
    explicit UsbFunctionfsTransport(bool autoInitialize);
    explicit UsbFunctionfsTransport(std::unique_ptr<LibusbBackend> backend, bool autoInitialize = true);
    ~UsbFunctionfsTransport() override;

    const Configuration &activeConfiguration() const override { return m_active; }
    bool hasStagedConfiguration() const override { return false; }
    TransportInfo info() const override;
    QJsonObject evidence() const override;
    ProductReadState readProductState() override;
    bool productOperationsCommissioned() const override;
    ProductOperationResult submitProductConfiguration(const productv9::ConfigWriteRequest &request, bool authorized) override;
    ProductOperationResult pollProductConfiguration(quint64 requestId) override;
    ProductOperationResult beginProductUpgrade(const productv9::UpgradePackage &package, bool authorized) override;
    ProductOperationResult writeProductUpgradeChunk(const QByteArray &data) override;
    ProductOperationResult finalizeProductUpgrade() override;
    ProductOperationResult abortProductUpgrade(quint32 reason, bool authorized) override;
    ProductOperationResult pollProductUpgrade() override;
    ProductOperationResult activateProductUpgrade(bool authorized) override;

    void disconnect() override;

    TransportResult stageRam(const Configuration &candidate) override;
    ReadbackResult readBackStaged() override;
    TransportResult confirmReadback() override;
    void rollbackStagedRam() override;
    WaveformSnapshot readWaveformSnapshot() override;
    bool supportsWaveformViewport() const override
    {
        return m_extendedDiscovery.available
            && m_extendedDiscovery.capabilities.protocolRevision == 9U
            && m_capabilities.waveformFlags != 0U;
    }
    WaveformViewportResult readWaveformViewport(
        const WaveformViewportRequest &request) override;
    TelemetrySnapshot readTelemetrySnapshot() override;
    DeviceLogListResult readLogSources() override;
    DeviceLogChunkResult readLogChunk(quint32 sourceId, quint64 offset,
                                      quint32 maximumBytes,
                                      quint64 snapshotId) override;
    ConfigurationReceiptDetails configurationReceiptDetails() const override;
    TransportResult reconnect() override;
    TransportPingResult ping(const QByteArray &payload) override;
    BinaryObjectResult readTelemetryObject() override;
    BinaryObjectResult readWaveformObject() override;
    BinaryObjectResult readConfigReceiptObject() override;
    UsbExtendedDiscoveryV2 usbExtendedDiscovery() const override
    {
        return m_extendedDiscovery;
    }
    BinaryObjectResult readUsbExtendedCapabilitiesObject() override;
    BinaryObjectResult readUsbParameterCatalogObject() override;
    UsbRuntimeConfigOperationResultV9 readRuntimeConfigV9(
        quint32 kind, quint64 transactionId = 0U) override;
    UsbRuntimeConfigOperationResultV9 submitRuntimeConfigV9(
        const UsbRuntimeConfigObjectV9 &object) override;
    ControlAuthorityResultV2 readControlAuthorityState() override;
    ControlAuthorityResultV2 switchControlMode(
        quint32 requestedMode) override;
    ControlAuthorityResultV2 renewControlLease() override;
    ControlAuthorityResultV2 resumeHostControl() override;
    BinaryObjectResult readControlAuthorityObject() override;
    RuntimeStatusResultV2 readRuntimeStatus() override;
    BinaryObjectResult readRuntimeStatusObject() override;
    UsbRuntimeProgressResultV9 readRuntimeProgressV9() override;
    UsbRuntimeActionResultV9 executeRuntimeActionV9(
        quint32 action, quint64 expectedGeneration) override;
    UsbCompositeRuntimeTrustResultV1 installCompositeRuntimeTrust(
        const UsbCompositeRuntimeTrustInputV1 &input,
        const UsbCompositeRuntimeManifestVerifierV1 &verifier) override;
    void clearCompositeRuntimeTrust(const QString &reason) override;
    UsbCompositeRuntimeStatusResultV1 readCompositeRuntimeStatus() override;
    BinaryObjectResult readCompositeRuntimeStatusObject() override;

    QByteArray readTelemetry(QString *error = nullptr);
    QByteArray listLogs(QString *error = nullptr);
    QByteArray readLog(quint32 sourceId, quint64 offset, quint32 maximumBytes,
                       quint64 snapshotId, QString *error = nullptr);
    QByteArray readWaveform(quint64 expectedGeneration,
                            quint32 sliceOffset,
                            QString *error = nullptr);

private:
    bool productWriteGate(QString *error);
    void ensureProductOperationClients();
    void invalidateProductOperations();
    bool transact(UsbMessageTypeV1 type, quint64 transactionId,
                  const QByteArray &requestPayload,
                  UsbFrameV1 *response, QString *error,
                  unsigned timeoutMs = 5000U,
                  qint32 *wireStatus = nullptr);
    bool initialize();
    void invalidateSession(const QString &reason);
    void discoverExtendedV2();
    ControlAuthorityResultV2 submitControlAuthorityAction(
        quint32 action, quint32 requestedMode);
    void recordControlAuthority(const ControlAuthorityStateV2 &state);
    void recordRuntimeStatus(const RuntimeStatusV2 &status);
    void recordCompositeRuntimeStatus(
        const UsbCompositeRuntimeStatusResultV1 &result);

    std::unique_ptr<productv9::ConfigOperationClient> m_productConfigClient;
    std::unique_ptr<productv9::UpgradeClient> m_productUpgradeClient;
    std::unique_ptr<LibusbBackend> m_backend;
    std::mutex m_transactionMutex;
    Configuration m_active;
    UsbCapabilitiesV1 m_capabilities;
    UsbExtendedDiscoveryV2 m_extendedDiscovery;
    QByteArray m_extendedCapabilitiesObject;
    QByteArray m_parameterCatalogObject;
    ControlAuthorityStateV2 m_controlAuthority;
    QByteArray m_controlAuthorityObject;
    RuntimeStatusV2 m_runtimeStatus;
    QByteArray m_runtimeStatusObject;
    UsbCompositeRuntimeTrustSourceV1 m_compositeRuntimeTrustSource;
    UsbCompositeRuntimeStatusV1 m_compositeRuntimeStatus;
    QByteArray m_compositeRuntimeStatusObject;
    QJsonObject m_protocolEvidence;
    QString m_lastError;
    ProductReadState m_productState;
    QByteArray m_productPublication, m_productInput;
    productv9::PublicationIdentity m_lastProductIdentity;
    QElapsedTimer m_productProgressTimer;
    QVector<productv9::LogEntry> m_logEntries;
    quint64 m_nextSequence = 1;
    quint64 m_transactions = 0;
    bool m_initialized = false;
    bool m_failurePreservesActive = false;
};

} // namespace ucm
