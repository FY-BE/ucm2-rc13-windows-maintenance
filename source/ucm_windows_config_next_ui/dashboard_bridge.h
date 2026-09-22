#pragma once

#include "config_session.h"
#include "dashboard_poll_worker.h"
#include "engineer_access.h"
#include "ethercat_port_probe.h"
#include "ethercat_master_controller.h"
#include "reference_force_source.h"
#include "calibration_controller.h"
#include "runtime_status_model.h"
#include "trend_buffer.h"
#include "usb_functionfs_transport.h"

#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class DashboardBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool offlinePreview READ offlinePreview CONSTANT)
    Q_PROPERTY(QVariantMap productCapabilities READ productCapabilities NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap productOperation READ productOperation NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap calibrationOperation READ calibrationOperation NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap calibrationReadback READ calibrationReadback NOTIFY stateChanged)
    Q_PROPERTY(bool productCanWrite READ productCanWrite NOTIFY stateChanged)
    Q_PROPERTY(bool productReadOnly READ productReadOnly NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap productState READ productState NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(bool telemetryReady READ telemetryReady NOTIFY telemetryChanged)
    Q_PROPERTY(bool runtimeReady READ runtimeReady NOTIFY telemetryChanged)
    Q_PROPERTY(QString runtimeStateText READ runtimeStateText NOTIFY telemetryChanged)
    Q_PROPERTY(QString runtimeDetailText READ runtimeDetailText NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap runtimeProgress READ runtimeProgress NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap runtimeActionResult READ runtimeActionResult NOTIFY stateChanged)
    Q_PROPERTY(QString usbLabel READ usbLabel NOTIFY connectionChanged)
    Q_PROPERTY(QString totalForceText READ totalForceText NOTIFY telemetryChanged)
    Q_PROPERTY(QString imbalanceText READ imbalanceText NOTIFY telemetryChanged)
    Q_PROPERTY(QString processStateText READ processStateText NOTIFY telemetryChanged)
    Q_PROPERTY(QStringList rodForceTexts READ rodForceTexts NOTIFY telemetryChanged)
    Q_PROPERTY(QStringList rodStrainTexts READ rodStrainTexts NOTIFY telemetryChanged)
    Q_PROPERTY(QStringList rodStateTexts READ rodStateTexts NOTIFY telemetryChanged)
    Q_PROPERTY(bool referenceForceRunning READ referenceForceRunning NOTIFY referenceForceChanged)
    Q_PROPERTY(bool referenceForceReady READ referenceForceReady NOTIFY referenceForceChanged)
    Q_PROPERTY(QString referenceForceStatus READ referenceForceStatus NOTIFY referenceForceChanged)
    Q_PROPERTY(QString referenceTotalForceText READ referenceTotalForceText NOTIFY referenceForceChanged)
    Q_PROPERTY(QString referenceTotalDifferenceText READ referenceTotalDifferenceText NOTIFY referenceForceChanged)
    Q_PROPERTY(QStringList referenceForceTexts READ referenceForceTexts NOTIFY referenceForceChanged)
    Q_PROPERTY(QStringList referenceDifferenceTexts READ referenceDifferenceTexts NOTIFY referenceForceChanged)
    Q_PROPERTY(QObject *calibration READ calibration CONSTANT)
    Q_PROPERTY(QObject *ethercatProbe READ ethercatProbe CONSTANT)
    Q_PROPERTY(QObject *ethercatMaster READ ethercatMaster CONSTANT)
    Q_PROPERTY(QVariantList diagnosticRods READ diagnosticRods NOTIFY diagnosticsChanged)
    Q_PROPERTY(QVariantMap diagnosticSummary READ diagnosticSummary NOTIFY diagnosticsChanged)
    Q_PROPERTY(QObject *trendBuffer READ trendBuffer CONSTANT)
    Q_PROPERTY(QVariantList waveformSeries READ waveformSeries NOTIFY waveformChanged)
    Q_PROPERTY(int waveformStart READ waveformStart NOTIFY waveformChanged)
    Q_PROPERTY(quint32 waveformSampleRateHz READ waveformSampleRateHz NOTIFY waveformChanged)
    Q_PROPERTY(QString waveformStatus READ waveformStatus NOTIFY waveformChanged)
    Q_PROPERTY(bool waveformBusy READ waveformBusy NOTIFY waveformChanged)
    Q_PROPERTY(bool waveformViewportAvailable READ waveformViewportAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool waveformViewportCanRead READ waveformViewportCanRead NOTIFY stateChanged)
    Q_PROPERTY(int waveformSliceOffset READ waveformSliceOffset NOTIFY waveformChanged)
    Q_PROPERTY(int waveformSliceMaximum READ waveformSliceMaximum CONSTANT)
    Q_PROPERTY(QString waveformViewportStatus READ waveformViewportStatus NOTIFY waveformChanged)
    Q_PROPERTY(QVariantList logSources READ logSources NOTIFY stateChanged)
    Q_PROPERTY(QString logStatus READ logStatus NOTIFY stateChanged)
    Q_PROPERTY(QString logContent READ logContent NOTIFY stateChanged)
    Q_PROPERTY(QString selectedLogTitle READ selectedLogTitle NOTIFY stateChanged)
    Q_PROPERTY(int selectedLogSourceId READ selectedLogSourceId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList timelineEvents READ timelineEvents NOTIFY stateChanged)
    Q_PROPERTY(QString timelineStatus READ timelineStatus NOTIFY stateChanged)
    Q_PROPERTY(QVariantList afeConfigurationFields READ afeConfigurationFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantList algorithmConfigurationFields READ algorithmConfigurationFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantList lockedConfigurationFields READ lockedConfigurationFields NOTIFY stateChanged)
    Q_PROPERTY(bool usbV2Available READ usbV2Available NOTIFY stateChanged)
    Q_PROPERTY(bool usbV2CatalogReady READ usbV2CatalogReady NOTIFY stateChanged)
    Q_PROPERTY(QString usbV2Status READ usbV2Status NOTIFY stateChanged)
    Q_PROPERTY(QVariantList usbV2SummaryItems READ usbV2SummaryItems NOTIFY stateChanged)
    Q_PROPERTY(QVariantList usbV2ParameterFields READ usbV2ParameterFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantList writableConfigurationFields READ writableConfigurationFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap runtimeBurstControl READ runtimeBurstControl NOTIFY stateChanged)
    Q_PROPERTY(QVariantList runtimeConfigurationFields READ runtimeConfigurationFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantList afeMaintenanceFields READ afeMaintenanceFields NOTIFY stateChanged)
    Q_PROPERTY(bool afeMaintenanceHasChanges READ afeMaintenanceHasChanges NOTIFY stateChanged)
    Q_PROPERTY(bool afeMaintenanceCanWrite READ afeMaintenanceCanWrite NOTIFY stateChanged)
    Q_PROPERTY(QString runtimeConfigurationStatus READ runtimeConfigurationStatus NOTIFY stateChanged)
    Q_PROPERTY(bool runtimeConfigurationBusy READ runtimeConfigurationBusy NOTIFY stateChanged)
    Q_PROPERTY(bool runtimeConfigurationHasChanges READ runtimeConfigurationHasChanges NOTIFY stateChanged)
    Q_PROPERTY(bool runtimeConfigurationCanWrite READ runtimeConfigurationCanWrite NOTIFY stateChanged)
    Q_PROPERTY(QString configurationStatus READ configurationStatus NOTIFY stateChanged)
    Q_PROPERTY(QString configurationIdentity READ configurationIdentity NOTIFY stateChanged)
    Q_PROPERTY(QStringList configurationChanges READ configurationChanges NOTIFY stateChanged)
    Q_PROPERTY(QStringList configurationErrors READ configurationErrors NOTIFY stateChanged)
    Q_PROPERTY(bool configurationHasChanges READ configurationHasChanges NOTIFY stateChanged)
    Q_PROPERTY(bool configurationPrepared READ configurationPrepared NOTIFY stateChanged)
    Q_PROPERTY(bool configurationCanApply READ configurationCanApply NOTIFY stateChanged)
    Q_PROPERTY(bool configurationBusy READ configurationBusy NOTIFY stateChanged)
    Q_PROPERTY(bool controlAuthorityAvailable READ controlAuthorityAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString deviceControlModeText READ deviceControlModeText NOTIFY stateChanged)
    Q_PROPERTY(QString deviceControlPhaseText READ deviceControlPhaseText NOTIFY stateChanged)
    Q_PROPERTY(QString controlAuthorityStatus READ controlAuthorityStatus NOTIFY stateChanged)
    Q_PROPERTY(QString controlLeaseText READ controlLeaseText NOTIFY stateChanged)
    Q_PROPERTY(bool controlModeBusy READ controlModeBusy NOTIFY stateChanged)
    Q_PROPERTY(bool canRequestManualControl READ canRequestManualControl NOTIFY stateChanged)
    Q_PROPERTY(bool canRequestAutonomousControl READ canRequestAutonomousControl NOTIFY stateChanged)
    Q_PROPERTY(bool canResumeManualControl READ canResumeManualControl NOTIFY stateChanged)
    Q_PROPERTY(QVariantList evidenceItems READ evidenceItems NOTIFY stateChanged)
    Q_PROPERTY(QString evidenceStatus READ evidenceStatus NOTIFY stateChanged)
    Q_PROPERTY(QString evidencePath READ evidencePath NOTIFY stateChanged)
    Q_PROPERTY(bool canExportEvidence READ canExportEvidence NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY telemetryChanged)
    Q_PROPERTY(QString eventText READ eventText NOTIFY telemetryChanged)
    Q_PROPERTY(bool engineerMode READ engineerMode NOTIFY accessModeChanged)
    Q_PROPERTY(bool engineerPinConfigured READ engineerPinConfigured NOTIFY accessModeChanged)
    Q_PROPERTY(QString accessMessage READ accessMessage NOTIFY accessModeChanged)

public:
    explicit DashboardBridge(QObject *parent = nullptr,
                             bool localSelfTestAccess = false,
                             bool offlinePreview = false);
    ~DashboardBridge() override;

    bool offlinePreview() const { return m_offlinePreview; }
    QObject *calibration() { return &m_calibration; }
    QObject *ethercatProbe() { return &m_ethercatProbe; }
    QObject *ethercatMaster() { return &m_ethercatMaster; }
    QVariantMap productCapabilities() const;
    QVariantMap productOperation() const { return m_productOperation; }
    QVariantMap calibrationOperation() const { return m_calibrationOperation; }
    QVariantMap calibrationReadback() const { return m_calibrationReadback; }
    bool productCanWrite() const;
    bool productReadOnly() const { return m_usbV2Discovery.capabilities.protocolRevision == 9; }
    QVariantMap productState() const;
    bool connected() const { return m_connected; }
    bool telemetryReady() const { return m_telemetryReady; }
    bool runtimeReady() const { return m_runtimeReady; }
    QString runtimeStateText() const { return m_runtimeStateText; }
    QString runtimeDetailText() const { return m_runtimeDetailText; }
    QVariantMap runtimeProgress() const { return m_runtimeProgress; }
    QVariantMap runtimeActionResult() const { return m_runtimeActionResult; }
    QString usbLabel() const { return m_usbLabel; }
    QString totalForceText() const { return m_totalForceText; }
    QString imbalanceText() const { return m_imbalanceText; }
    QString processStateText() const { return m_processStateText; }
    QStringList rodForceTexts() const { return m_rodForceTexts; }
    QStringList rodStrainTexts() const { return m_rodStrainTexts; }
    QStringList rodStateTexts() const { return m_rodStateTexts; }
    bool referenceForceRunning() const { return m_referenceForce.running(); }
    bool referenceForceReady() const { return m_referenceForce.ready(); }
    QString referenceForceStatus() const { return m_referenceForce.statusText(); }
    QString referenceTotalForceText() const;
    QString referenceTotalDifferenceText() const;
    QStringList referenceForceTexts() const;
    QStringList referenceDifferenceTexts() const;
    QVariantList diagnosticRods() const { return m_diagnosticRods; }
    QVariantMap diagnosticSummary() const { return m_diagnosticSummary; }
    QObject *trendBuffer() { return &m_trendBuffer; }
    QVariantList waveformSeries() const { return m_waveformSeries; }
    int waveformStart() const { return m_waveformStart; }
    quint32 waveformSampleRateHz() const { return m_waveformSampleRateHz; }
    QString waveformStatus() const { return m_waveformStatus; }
    bool waveformBusy() const { return m_waveformBusy; }
    bool waveformViewportAvailable() const
    { return m_waveformViewportAvailable; }
    bool waveformViewportCanRead() const;
    int waveformSliceOffset() const { return m_waveformSliceOffset; }
    int waveformSliceMaximum() const { return 6144; }
    QString waveformViewportStatus() const { return m_waveformViewportStatus; }
    QVariantList logSources() const { return m_logSources; }
    QString logStatus() const { return m_logStatus; }
    QString logContent() const { return m_logContent; }
    QString selectedLogTitle() const { return m_selectedLogTitle; }
    int selectedLogSourceId() const { return m_selectedLogSourceId; }
    QVariantList timelineEvents() const { return m_timelineEvents; }
    QString timelineStatus() const { return m_timelineStatus; }
    QVariantList afeConfigurationFields() const;
    QVariantList algorithmConfigurationFields() const;
    QVariantList lockedConfigurationFields() const;
    bool usbV2Available() const { return m_usbV2Discovery.available; }
    bool usbV2CatalogReady() const { return m_usbV2Discovery.catalogReady; }
    QString usbV2Status() const { return m_usbV2Discovery.message; }
    QVariantList usbV2SummaryItems() const;
    QVariantList usbV2ParameterFields() const;
    QVariantList writableConfigurationFields() const;
    QVariantMap runtimeBurstControl() const;
    QVariantList runtimeConfigurationFields() const;
    QVariantList afeMaintenanceFields() const;
    bool afeMaintenanceHasChanges() const;
    bool afeMaintenanceCanWrite() const { return runtimeConfigurationCanWrite(); }
    QString runtimeConfigurationStatus() const { return m_runtimeParameterStatus; }
    bool runtimeConfigurationBusy() const { return m_runtimeParameterBusy; }
    bool runtimeConfigurationHasChanges() const { return !m_runtimeParameterDraft.isEmpty(); }
    bool runtimeConfigurationCanWrite() const;
    QString configurationStatus() const { return m_configurationStatus; }
    QString configurationIdentity() const { return m_configurationIdentity; }
    QStringList configurationChanges() const { return m_configurationChanges; }
    QStringList configurationErrors() const { return m_configurationErrors; }
    bool configurationHasChanges() const;
    bool configurationPrepared() const;
    bool configurationCanApply() const;
    bool configurationBusy() const { return m_configurationBusy; }
    bool controlAuthorityAvailable() const
    {
        return m_controlAuthority.available;
    }
    QString deviceControlModeText() const;
    QString deviceControlPhaseText() const;
    QString controlAuthorityStatus() const
    {
        return m_controlAuthorityStatus;
    }
    QString controlLeaseText() const;
    bool controlModeBusy() const { return m_controlModeBusy; }
    bool canRequestManualControl() const;
    bool canRequestAutonomousControl() const;
    bool canResumeManualControl() const;
    QVariantList evidenceItems() const { return m_evidenceItems; }
    QString evidenceStatus() const { return m_evidenceStatus; }
    QString evidencePath() const { return m_evidencePath; }
    bool canExportEvidence() const
    {
        return (m_access.engineerMode() || productReadOnly()) && m_connected
            && !m_exportBusy && !m_productOperationBusy && !m_configurationBusy && !m_authorityOperationInFlight
            && !m_authorityOperationQueued;
    }
    QString statusText() const { return m_statusText; }
    QString eventText() const { return m_eventText; }
    bool engineerMode() const { return m_access.engineerMode(); }
    bool engineerPinConfigured() const { return m_access.pinConfigured(); }
    QString accessMessage() const { return m_access.message(); }

    Q_INVOKABLE void submitProductDocument(int domain, int operation, const QString &json, bool authorized);
    Q_INVOKABLE void submitCalibrationCandidate(int operation, bool authorized);
    Q_INVOKABLE void submitCustomerProduct(int operation, const QVariantMap &draft, bool authorized);
    Q_INVOKABLE void stageProductUpgrade(const QUrl &file, int version, const QString &model, const QString &buildId, bool authorized);
    Q_INVOKABLE void activateProductUpgrade(bool authorized);
    Q_INVOKABLE void queryProductOperation();
    Q_INVOKABLE void abortProductUpgrade(bool authorized);
    Q_INVOKABLE void exportCsv(const QUrl &destination);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void toggleReferenceForce();
    Q_INVOKABLE void setActivePage(int page);
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void refreshCalibrationReadback();
    Q_INVOKABLE void captureLatestWaveform();
    Q_INVOKABLE void shiftWaveformViewport(int direction);
    Q_INVOKABLE void requestRuntimeAction(int action);
    Q_INVOKABLE void refreshLogSources();
    Q_INVOKABLE void loadLogSource(int sourceId);
    Q_INVOKABLE void refreshTimeline();
    Q_INVOKABLE void setConfigurationField(const QString &key,
                                           const QVariant &value);
    Q_INVOKABLE void resetConfigurationDraft();
    Q_INVOKABLE void prepareConfiguration();
    Q_INVOKABLE void applyPreparedConfiguration();
    Q_INVOKABLE void requestManualControl();
    Q_INVOKABLE void requestAutonomousControl();
    Q_INVOKABLE void resumeManualControl();
    Q_INVOKABLE void setRuntimeBurstDraft(int cycles);
    Q_INVOKABLE void applyRuntimeBurst(bool authorized);
    Q_INVOKABLE void saveRuntimeBurstStartup(bool authorized);
    Q_INVOKABLE void setRuntimeConfigurationField(
        int fieldId, int elementIndex, const QVariant &value);
    Q_INVOKABLE void resetRuntimeConfigurationDraft();
    Q_INVOKABLE void applyRuntimeConfiguration(bool authorized);
    Q_INVOKABLE void resetAfeMaintenanceDraft();
    Q_INVOKABLE void applyAfeMaintenance(bool authorized);
    Q_INVOKABLE void saveRuntimeConfigurationStartup(bool authorized);
    Q_INVOKABLE void refreshEvidenceSummary();
    Q_INVOKABLE void exportDiagnosticPackage(const QUrl &destination);
    Q_INVOKABLE bool configureEngineerPin(const QString &pin,
                                          const QString &confirmation);
    Q_INVOKABLE bool enterEngineerMode(const QString &pin);
    Q_INVOKABLE void leaveEngineerMode();

signals:
    void calibrationReadbackRequested();
    void stateChanged();
    void connectionChanged();
    void telemetryChanged();
    void referenceForceChanged();
    void diagnosticsChanged();
    void waveformChanged();
    void accessModeChanged();
    void productDocumentRequested(int domain, int operation, const QByteArray &json, bool authorized,
                                  const QString &expectedActiveId);
    void productUpgradeRequested(const QString &path, quint32 version, const QString &model, const QString &buildId, bool authorized);
    void productActivationRequested(bool authorized);
    void productQueryRequested();
    void productAbortRequested(bool authorized);
    void productPackageRequested(const QString &directory, const QByteArray &csv);
    void logSourcesRequested(quint64 token);
    void logChunkRequested(quint64 token, quint32 sourceId, quint64 snapshotId);
    void reconnectRequested();
    void disconnectRequested();
    void pollRequested(bool includeWaveform);
    void waveformReadRequested();
    void waveformViewportRequested(quint32 sliceOffset,
                                   quint64 expectedGeneration);
    void configurationApplyRequested();
    void controlAuthorityRequested(ControlAuthorityOperation operation,
                                   quint32 requestedMode);
    void runtimeBurstOperationRequested(int operation, int cycles);
    void runtimeParameterOperationRequested(int operation,
                                            const QVariantList &edits);
    void runtimeActionRequested(quint32 action, quint64 expectedGeneration);

private:
    void finishLogSources(quint64 token, const ucm::DeviceLogListResult &result);
    void finishLogChunk(quint64 token, const ucm::DeviceLogChunkResult &result);
    void applyPollResult(const DashboardPollResult &result);
    void dispatchPreparedConfiguration();
    void finishPreparedConfiguration(
        const ConfigurationApplyResult &result);
    void dispatchControlAuthority(ControlAuthorityOperation operation,
                                  quint32 requestedMode,
                                  bool userInitiated);
    void finishControlAuthority(
        const ControlAuthorityOperationResult &result);
    void applyControlAuthorityState(
        const ucm::ControlAuthorityStateV2 &state);
    void serviceControlAuthority();
    void queueReferenceUiRefresh();
    void refreshConnectionState();
    void clearMeasurements(const QString &rodState);
    void clearDiagnostics(const QString &state);
    void clearWaveform(const QString &status);
    void applyWaveformSnapshot(const ucm::WaveformSnapshot &snapshot);
    bool engineerAuthorized() const
    {
        return m_access.engineerMode() || m_localSelfTestAccess;
    }

    void scheduleReconnect();
    bool m_manuallyDisconnected = false;
    bool m_reconnectScheduled = false;
    int m_reconnectAttempt = 0;
    ucm::ProductReadState m_product;
    ucm::ConfigurationSession m_session;
    EngineerAccess m_access;
    bool m_localSelfTestAccess = false;
    bool m_offlinePreview = false;
    EthercatPortProbe m_ethercatProbe;
    EthercatMasterController m_ethercatMaster;
    mutable QMutex m_sessionMutex;
    QThread m_pollThread;
    DashboardPollWorker *m_pollWorker = nullptr;
    ReferenceForceSource m_referenceForce;
    CalibrationController m_calibration {&m_referenceForce};
    QTimer m_timer;
    QTimer m_authorityTimer;
    QTimer m_referenceUiTimer;
    QElapsedTimer m_pollClock;
    bool m_pollInFlight = false;
    bool m_authorityOperationInFlight = false;
    bool m_authorityOperationQueued = false;
    bool m_queuedAuthorityUserInitiated = false;
    bool m_controlModeBusy = false;
    ControlAuthorityOperation m_queuedAuthorityOperation =
        ControlAuthorityOperation::Refresh;
    quint32 m_queuedAuthorityMode = 0;
    bool m_connectionStateInitialized = false;
    int m_activePage = 0;
    qint64 m_lastWaveformPollMs = -1000;
    qint64 m_lastTelemetryPollMs = -1;
    bool m_connected = false;
    bool m_telemetryReady = false;
    bool m_runtimeReady = false;
    QString m_runtimeStateText = QStringLiteral("等待读取ARM运行状态");
    QString m_runtimeDetailText;
    QVariantMap m_runtimeProgress;
    QVariantMap m_runtimeActionResult;
    bool m_runtimeActionBusy = false;
    QString m_usbLabel = QStringLiteral("USB 待连接");
    QString m_totalForceText = QStringLiteral("--");
    QString m_imbalanceText = QStringLiteral("--");
    QString m_processStateText = QStringLiteral("待机");
    QStringList m_rodForceTexts {
        QStringLiteral("--"), QStringLiteral("--"),
        QStringLiteral("--"), QStringLiteral("--")
    };
    QStringList m_rodStrainTexts {
        QStringLiteral("--"), QStringLiteral("--"),
        QStringLiteral("--"), QStringLiteral("--")
    };
    QStringList m_rodStateTexts {
        QStringLiteral("等待真实遥测"), QStringLiteral("等待真实遥测"),
        QStringLiteral("等待真实遥测"), QStringLiteral("等待真实遥测")
    };
    std::array<double, 4> m_lastRodForceN {};
    quint32 m_lastForceAvailableMask = 0;
    double m_lastFormalTotalN = 0.0;
    bool m_lastFormalTotalValid = false;
    QVariantList m_diagnosticRods;
    QVariantMap m_diagnosticSummary;
    TrendBuffer m_trendBuffer;
    QVariantList m_waveformSeries;
    quint64 m_waveformGeneration = 0;
    int m_waveformStart = 0;
    quint32 m_waveformSampleRateHz = 0;
    QString m_waveformStatus = QStringLiteral("等待真实USB波形");
    bool m_waveformBusy = false;
    bool m_waveformViewportAvailable = false;
    int m_waveformSliceOffset = 0;
    QString m_waveformViewportStatus = QStringLiteral(
        "读取最新波形后可浏览ARM保留的8192点窗口。");
    void recordArmSnapshot(const ucm::TelemetrySnapshot &snapshot);
    void recordCsvGap(const QString &reason = QStringLiteral("USB断开或当前数据不可用"));
    std::deque<QByteArray> m_csvRows;
    QString m_lastCsvIdentity;
    bool m_csvGap = true;
    QVariantMap m_productOperation;
    QVariantMap m_calibrationOperation;
    QVariantMap m_calibrationReadback;
    int m_calibrationPendingOperation = 0;
    QString m_calibrationPendingSession;
    QString m_calibrationValidatedSession;
    QString m_calibrationAppliedSession;
    bool m_productCommissioned = false;
    bool m_productOperationBusy = false;
    QByteArray armForceCsv() const;
    bool m_exportBusy = false;
    bool m_logBusy = false;
    quint64 m_logToken = 0;
    QVector<ucm::DeviceLogSource> m_logSourceRecords;
    QVariantList m_logSources;
    QString m_logStatus = QStringLiteral("进入运行日志页后读取固定白名单源");
    QString m_logContent;
    QString m_selectedLogTitle = QStringLiteral("尚未选择日志源");
    int m_selectedLogSourceId = 0;
    QVariantList m_timelineEvents;
    QString m_timelineStatus = QStringLiteral("进入事件页后读取固定 source 4");
    QString m_configurationStatus;
    QString m_configurationIdentity;
    QStringList m_configurationChanges;
    QStringList m_configurationErrors;
    bool m_configurationRequiresReconnect = false;
    bool m_configurationBusy = false;
    bool m_configurationApplyQueued = false;
    ucm::UsbExtendedDiscoveryV2 m_usbV2Discovery;
    ucm::UsbRuntimeConfigOperationResultV9 m_runtimeConfig;
    int m_runtimeBurstDraft = 1;
    bool m_runtimeBurstDraftDirty = false;
    bool m_runtimeBurstBusy = false;
    QString m_runtimeBurstStatus = QStringLiteral("等待读取ARM当前burst配置");
    QMap<QString, qint64> m_runtimeParameterDraft;
    bool m_runtimeParameterBusy = false;
    QString m_runtimeParameterStatus = QStringLiteral(
        "等待读取ARM revision 9运行配置");
    ucm::ControlAuthorityStateV2 m_controlAuthority;
    QString m_controlAuthorityStatus = QStringLiteral(
        "等待读取ARM控制权状态");
    QVariantList m_evidenceItems;
    QString m_evidenceStatus = QStringLiteral("等待核对诊断包来源");
    QString m_evidencePath;
    QString m_statusText = QStringLiteral("正在检查真实WinUSB设备…");
    QString m_eventText = QStringLiteral("等待真实设备握手");
};
