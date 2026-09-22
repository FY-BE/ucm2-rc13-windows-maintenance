#pragma once

#include "config_session.h"
#include "diagnostic_package_writer.h"
#include "product_log_download.h"
#include "usb_product_operations_v9.h"
#include <QFile>
#include <QJsonArray>

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>

struct DashboardPollResult {
    bool productCommissioned = false;
    ucm::UsbExtendedDiscoveryV2 discovery;
    ucm::ProductReadState product;
    ucm::TransportInfo transport;
    ucm::RuntimeStatusResultV2 runtime;
    ucm::UsbRuntimeConfigOperationResultV9 runtimeConfig;
    ucm::UsbCompositeRuntimeStatusResultV1 compoundRuntime;
    ucm::UsbRuntimeProgressResultV9 runtimeProgress;
    ucm::TelemetrySnapshot telemetry;
    bool waveformRequested = false;
    ucm::WaveformSnapshot waveform;
    bool diagnosticWaveformCaptureSupported = false;
};

Q_DECLARE_METATYPE(DashboardPollResult)
Q_DECLARE_METATYPE(ucm::ProductOperationResult)
Q_DECLARE_METATYPE(ucm::UsbRuntimeConfigOperationResultV9)
Q_DECLARE_METATYPE(ucm::DiagnosticPackageResult)
Q_DECLARE_METATYPE(ucm::DeviceLogListResult)
Q_DECLARE_METATYPE(ucm::DeviceLogChunkResult)
Q_DECLARE_METATYPE(ucm::UsbRuntimeActionResultV9)

struct DashboardWaveformResult {
    bool viewportRead = false;
    bool supported = false;
    QString message;
    quint32 sliceOffset = 0;
    ucm::WaveformSnapshot waveform;
};

Q_DECLARE_METATYPE(DashboardWaveformResult)

struct ConfigurationApplyResult {
    bool committed = false;
    bool confirmed = false;
    bool failurePreservesActive = false;
    QString message;
    ucm::ConfigIdentity activeIdentity;
};

Q_DECLARE_METATYPE(ConfigurationApplyResult)

enum class ControlAuthorityOperation : int {
    Refresh = 0,
    SwitchMode = 1,
    RenewLease = 2,
    ResumeHost = 3
};

struct ControlAuthorityOperationResult {
    ControlAuthorityOperation operation = ControlAuthorityOperation::Refresh;
    quint32 requestedMode = 0;
    ucm::ControlAuthorityResultV2 result;
    bool productCommissioned = false;
    ucm::UsbExtendedDiscoveryV2 discovery;
};

Q_DECLARE_METATYPE(ControlAuthorityOperation)
Q_DECLARE_METATYPE(ControlAuthorityOperationResult)

class DashboardPollWorker final : public QObject {
    Q_OBJECT

public:
    explicit DashboardPollWorker(ucm::ConfigurationSession *session,
                                 QMutex *sessionMutex,
                                 QObject *parent = nullptr);

public slots:
    void readCalibrationParameters();
    void submitProductDocument(int domain, int operation, const QByteArray &json, bool authorized,
                               const QString &expectedActiveId = QString());
    void stageProductUpgrade(const QString &path, quint32 version, const QString &model, const QString &buildId, bool authorized);
    void activateProductUpgrade(bool authorized);
    void queryProductOperation();
    void abortProductUpgrade(bool authorized);
    void exportProductPackage(const QString &directory, const QByteArray &csv);
    void readLogSources(quint64 token);
    void readLogChunk(quint64 token, quint32 sourceId, quint64 snapshotId);
    void reconnectTransport();
    void disconnectTransport();
    void poll(bool includeWaveform);
    void readLatestWaveform();
    void readWaveformViewport(quint32 sliceOffset,
                              quint64 expectedGeneration);
    void applyPreparedConfiguration();
    void controlAuthority(ControlAuthorityOperation operation,
                          quint32 requestedMode);
    void runtimeBurstOperation(int operation, int cycles);
    void runtimeParameterOperation(int operation, const QVariantList &edits);
    void runtimeAction(quint32 action, quint64 expectedGeneration);

signals:
    void calibrationParametersRead(const QVariantMap &result);
    void productOperationUpdated(const ucm::ProductOperationResult &result, bool busy);
    void productPackageCompleted(const ucm::DiagnosticPackageResult &result);
    void productPackageProgress(const QString &message);
    void logSourcesCompleted(quint64 token, const ucm::DeviceLogListResult &result);
    void logChunkCompleted(quint64 token, const ucm::DeviceLogChunkResult &result);
    void completed(const DashboardPollResult &result);
    void waveformReadCompleted(const DashboardWaveformResult &result);
    void configurationApplied(const ConfigurationApplyResult &result);
    void controlAuthorityCompleted(
        const ControlAuthorityOperationResult &result);
    void runtimeBurstOperationCompleted(
        const ucm::UsbRuntimeConfigOperationResultV9 &result);
    void runtimeParameterOperationCompleted(
        const ucm::UsbRuntimeConfigOperationResultV9 &result);
    void runtimeActionCompleted(const ucm::UsbRuntimeActionResultV9 &result);

private:
    void pollProductOperation();
    void finishOperationQuery(ucm::ProductOperationResult result, bool automatic);
    void nextUpgradeChunk();
    void cancelProductOperation();
    void reportProductOperation(const ucm::ProductOperationResult &result, bool busy);
    bool m_productOperationActive = false;
    bool m_upgradeOperation = false;
    bool m_activationNeedsQuery = false;
    bool m_queryAfterReconnect = false;
    bool m_wasConnected = false;
    QString m_upgradePhase;
    QElapsedTimer m_operationAge;
    QElapsedTimer m_runtimeAge;
    QElapsedTimer m_runtimeConfigAge;
    QElapsedTimer m_runtimeProgressAge;
    ucm::RuntimeStatusResultV2 m_cachedRuntime;
    ucm::UsbCompositeRuntimeStatusResultV1 m_cachedCompound;
    ucm::UsbRuntimeConfigOperationResultV9 m_cachedRuntimeConfig;
    ucm::UsbRuntimeProgressResultV9 m_cachedRuntimeProgress;
    ucm::ProductOperationResult m_lastProductOperation;
    int m_operationPolls = 0;
    quint64 m_requestCounter = 0;
    QFile m_upgradeFile;
    quint32 m_upgradeChunkBytes = 65536;
    void exportNextChunk();
    void finishProductExport();
    void cancelProductExport();
    bool m_exportActive = false;
    QString m_exportDirectory;
    QVector<ucm::DeviceLogSource> m_exportSources;
    qsizetype m_exportIndex = 0;
    ucm::ProductLogDownload m_download;
    QVector<ucm::DiagnosticPackageFile> m_exportFiles;
    QJsonObject m_exportManifest;
    QJsonArray m_exportErrors;
    QJsonArray m_exportLogMetadata;
    ucm::ProductReadState m_product;
    QElapsedTimer m_productAge;
    ucm::ConfigurationSession *m_session = nullptr;
    QMutex *m_sessionMutex = nullptr;
};
