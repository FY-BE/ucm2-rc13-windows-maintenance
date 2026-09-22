#include "arm_force_csv.h"
#include <QSaveFile>
#include <QJsonDocument>
#include "dashboard_bridge.h"
#include "customer_product_draft.h"

#include "compound_runtime_status_model.h"
#include "diagnostic_package_writer.h"
#include "event_timeline.h"
#include "poll_schedule.h"
#include "reason_dictionary.h"
#include "usb_extended_wire_v2.h"
#include "usb_runtime_parameter_edit_v9.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QVariantMap>

#include <algorithm>
#include <limits>
#include <cmath>
#include <memory>

namespace {

QString hexValue(quint64 value, int width)
{
    return QStringLiteral("0x%1")
        .arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString byteSize(quint64 bytes)
{
    if (bytes >= 1024U * 1024U) {
        return QStringLiteral("%1 MiB")
            .arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024U) {
        return QStringLiteral("%1 KiB")
            .arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

bool hasCalibrationCouplingBias(const QJsonObject &document)
{
    const QJsonValue value = document.value(QStringLiteral("coupling_bias_ns"));
    if (!value.isArray()) return false;
    const QJsonArray offsets = value.toArray();
    if (offsets.size() != 4 || !offsets.at(0).isDouble()
        || !std::isfinite(offsets.at(0).toDouble())) return false;
    for (const auto &offset : offsets) {
        if (!offset.isDouble() || !std::isfinite(offset.toDouble()))
            return false;
    }
    return true;
}

bool forceCorrectionFields(const QJsonObject &candidate, QJsonObject *fields)
{
    if (fields == nullptr) return false;
    const QString schema = candidate.value(QStringLiteral("schema")).toString();
    QJsonArray inputs;
    QJsonArray outputs;
    int count = 0;
    if (schema == QStringLiteral("ucm-windows-calibration-candidate/v3")) {
        const QJsonObject correction =
            candidate.value(QStringLiteral("force_correction")).toObject();
        const QString model = correction.value(QStringLiteral("model")).toString();
        count = correction.value(QStringLiteral("knot_count")).toInt(-1);
        inputs = correction.value(QStringLiteral("input_force_n")).toArray();
        outputs = correction.value(QStringLiteral("output_force_n")).toArray();
        if (model == QStringLiteral("IDENTITY")) {
            if (count != 0 || !inputs.isEmpty() || !outputs.isEmpty()) return false;
        } else if (model == QStringLiteral("MONOTONE_PWL_ZERO_V1")) {
            if (count < 2 || count > 8 || inputs.size() != count || outputs.size() != count)
                return false;
            for (int index = 0; index < count; ++index) {
                if (!inputs[index].isDouble() || !outputs[index].isDouble() ||
                    !std::isfinite(inputs[index].toDouble()) ||
                    !std::isfinite(outputs[index].toDouble()) ||
                    (index == 0 && (inputs[index].toDouble() != 0.0 ||
                                    outputs[index].toDouble() != 0.0)) ||
                    (index > 0 && (inputs[index].toDouble() <= inputs[index - 1].toDouble() ||
                                   outputs[index].toDouble() <= outputs[index - 1].toDouble())))
                    return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
    while (inputs.size() < 8) inputs.append(0.0);
    while (outputs.size() < 8) outputs.append(0.0);
    fields->insert(QStringLiteral("force_correction_knot_count"), count);
    fields->insert(QStringLiteral("force_correction_input_n"), inputs);
    fields->insert(QStringLiteral("force_correction_output_n"), outputs);
    return true;
}

} // namespace

DashboardBridge::DashboardBridge(QObject *parent, bool localSelfTestAccess, bool offlinePreview)
    : QObject(parent)
    , m_session(std::make_unique<ucm::UsbFunctionfsTransport>(false))
    , m_access()
    , m_localSelfTestAccess(localSelfTestAccess)
    , m_offlinePreview(offlinePreview || localSelfTestAccess)
    , m_ethercatProbe(!(offlinePreview || localSelfTestAccess))
    , m_ethercatMaster(!(offlinePreview || localSelfTestAccess))
{
    qRegisterMetaType<DashboardPollResult>();
    qRegisterMetaType<ucm::ProductOperationResult>();
    qRegisterMetaType<ucm::UsbRuntimeConfigOperationResultV9>();
    qRegisterMetaType<ucm::DiagnosticPackageResult>();
    qRegisterMetaType<ucm::DeviceLogListResult>();
    qRegisterMetaType<ucm::DeviceLogChunkResult>();
    qRegisterMetaType<ConfigurationApplyResult>();
    qRegisterMetaType<ControlAuthorityOperation>();
    qRegisterMetaType<ControlAuthorityOperationResult>();
    qRegisterMetaType<ucm::UsbRuntimeActionResultV9>();
    qRegisterMetaType<DashboardWaveformResult>();
    m_pollWorker = new DashboardPollWorker(&m_session, &m_sessionMutex);
    m_pollWorker->moveToThread(&m_pollThread);
    connect(this, &DashboardBridge::calibrationReadbackRequested,
        m_pollWorker, &DashboardPollWorker::readCalibrationParameters, Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::calibrationParametersRead, this,
        [this](const QVariantMap &result) {
            m_calibrationReadback = result;
            if (!m_connected) {
                m_calibrationReadback = {{"busy", false}, {"outcome", "failed"},
                    {"message", QStringLiteral("USB已断开，当前ARM参数未知")}};
            } else if (result.value(QStringLiteral("outcome")).toString() == QStringLiteral("succeeded")) {
                m_product.deviceModel.insert(QStringLiteral("available"), true);
                for (const auto *key : {"active_document", "startup_document", "active_identity", "startup_identity"})
                    m_product.deviceModel.insert(QString::fromLatin1(key), QJsonObject::fromVariantMap(result.value(QString::fromLatin1(key)).toMap()));
            }
            emit stateChanged();
        });
    connect(this, &DashboardBridge::productQueryRequested, m_pollWorker, &DashboardPollWorker::queryProductOperation, Qt::QueuedConnection);
    connect(this, &DashboardBridge::productAbortRequested, m_pollWorker, &DashboardPollWorker::abortProductUpgrade, Qt::QueuedConnection);
    connect(this, &DashboardBridge::productDocumentRequested,
            m_pollWorker, &DashboardPollWorker::submitProductDocument, Qt::QueuedConnection);
    connect(this, &DashboardBridge::productUpgradeRequested,
            m_pollWorker, &DashboardPollWorker::stageProductUpgrade, Qt::QueuedConnection);
    connect(this, &DashboardBridge::productActivationRequested,
            m_pollWorker, &DashboardPollWorker::activateProductUpgrade, Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::productOperationUpdated, this,
        [this](const ucm::ProductOperationResult &result, bool busy) {
            m_productOperationBusy = busy;
            m_productOperation = {{QStringLiteral("ok"), result.ok},
                {QStringLiteral("outcome"), result.outcome},
                {QStringLiteral("message"), result.message},
                {QStringLiteral("busy"), busy},
                {QStringLiteral("phase"), result.fields.value(QStringLiteral("phase")).toString()},
                {QStringLiteral("fields"), result.fields.toVariantMap()}};
            if (m_calibrationPendingOperation != 0) {
                m_calibrationOperation = m_productOperation;
                m_calibrationOperation.insert(QStringLiteral("operation"), m_calibrationPendingOperation);
                const bool evidenceSaved = busy || m_calibration.recordArmOperation(m_calibrationPendingOperation, m_calibrationOperation);
                if (!evidenceSaved) {
                    m_calibrationOperation.insert(QStringLiteral("evidenceWarning"),
                        QStringLiteral("ARM 操作结果未能写入标定会话证据，请另存诊断记录"));
                }
                if (result.outcome == QStringLiteral("succeeded") && evidenceSaved) {
                    if (m_calibrationPendingOperation == 3)
                        m_calibrationValidatedSession = m_calibrationPendingSession;
                    else if (m_calibrationPendingOperation == 1)
                        m_calibrationAppliedSession = m_calibrationPendingSession;
                }
                if (result.outcome != QStringLiteral("pending")) {
                    const bool readback = result.outcome == QStringLiteral("succeeded");
                    m_calibrationPendingOperation = 0;
                    m_calibrationPendingSession.clear();
                    if (readback) refreshCalibrationReadback();
                }
            }
            emit stateChanged();
        });
    connect(this, &DashboardBridge::productPackageRequested,
            m_pollWorker, &DashboardPollWorker::exportProductPackage, Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::productPackageProgress, this,
        [this](const QString &message) { m_evidenceStatus = message; emit stateChanged(); });
    connect(m_pollWorker, &DashboardPollWorker::productPackageCompleted, this,
        [this](const ucm::DiagnosticPackageResult &result) {
            m_exportBusy = false;
            m_evidenceStatus = result.success ? QStringLiteral("产品诊断包已导出；含原始日志、状态、能力与SHA256清单") : result.message;
            m_evidencePath = result.packagePath;
            emit stateChanged();
        });
    connect(this, &DashboardBridge::logSourcesRequested,
            m_pollWorker, &DashboardPollWorker::readLogSources, Qt::QueuedConnection);
    connect(this, &DashboardBridge::logChunkRequested,
            m_pollWorker, &DashboardPollWorker::readLogChunk, Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::logSourcesCompleted,
            this, &DashboardBridge::finishLogSources, Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::logChunkCompleted,
            this, &DashboardBridge::finishLogChunk, Qt::QueuedConnection);
    connect(this, &DashboardBridge::reconnectRequested,
            m_pollWorker, &DashboardPollWorker::reconnectTransport, Qt::QueuedConnection);
    connect(this, &DashboardBridge::disconnectRequested,
            m_pollWorker, &DashboardPollWorker::disconnectTransport, Qt::QueuedConnection);
    connect(this, &DashboardBridge::pollRequested,
            m_pollWorker, &DashboardPollWorker::poll,
            Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::completed,
            this, &DashboardBridge::applyPollResult,
            Qt::QueuedConnection);
    connect(this, &DashboardBridge::waveformReadRequested,
            m_pollWorker, &DashboardPollWorker::readLatestWaveform,
            Qt::QueuedConnection);
    connect(this, &DashboardBridge::waveformViewportRequested,
            m_pollWorker, &DashboardPollWorker::readWaveformViewport,
            Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::waveformReadCompleted,
            this, [this](const DashboardWaveformResult &result) {
                m_waveformBusy = false;
                m_waveformViewportAvailable = result.supported
                    || m_waveformViewportAvailable;
                m_waveformViewportStatus = result.message;
                if (result.waveform.success) {
                    if (result.viewportRead)
                        m_waveformSliceOffset = static_cast<int>(result.sliceOffset);
                    else
                        m_waveformSliceOffset = 0;
                    applyWaveformSnapshot(result.waveform);
                } else if (!result.message.isEmpty())
                    m_waveformStatus = result.message;
                emit waveformChanged();
                emit stateChanged();
            }, Qt::QueuedConnection);
    connect(this, &DashboardBridge::configurationApplyRequested,
            m_pollWorker,
            &DashboardPollWorker::applyPreparedConfiguration,
            Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::configurationApplied,
            this, &DashboardBridge::finishPreparedConfiguration,
            Qt::QueuedConnection);
    connect(this, &DashboardBridge::controlAuthorityRequested,
            m_pollWorker, &DashboardPollWorker::controlAuthority,
            Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::controlAuthorityCompleted,
            this, &DashboardBridge::finishControlAuthority,
            Qt::QueuedConnection);
    connect(this, &DashboardBridge::runtimeBurstOperationRequested,
            m_pollWorker, &DashboardPollWorker::runtimeBurstOperation,
            Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::runtimeBurstOperationCompleted,
            this, [this](const ucm::UsbRuntimeConfigOperationResultV9 &result) {
                m_runtimeBurstBusy = false;
                m_runtimeConfig = result;
                m_runtimeBurstStatus = result.message;
                if (result.success
                    && (result.receipt.kind
                            == ucm::UsbRuntimeConfigReceiptAppliedV9
                        || result.receipt.kind
                            == ucm::UsbRuntimeConfigReceiptSavedStartupV9)) {
                    m_runtimeBurstDraft = static_cast<int>(
                        result.receipt.activeConfiguration.txBurstCycles);
                    m_runtimeBurstDraftDirty = false;
                }
                emit stateChanged();
                refreshNow();
            }, Qt::QueuedConnection);
    connect(this, &DashboardBridge::runtimeParameterOperationRequested,
            m_pollWorker, &DashboardPollWorker::runtimeParameterOperation,
            Qt::QueuedConnection);
    connect(m_pollWorker,
            &DashboardPollWorker::runtimeParameterOperationCompleted,
            this, [this](
                const ucm::UsbRuntimeConfigOperationResultV9 &result) {
                m_runtimeParameterBusy = false;
                m_runtimeParameterStatus = result.message;
                if (result.success) {
                    m_runtimeConfig = result;
                    if (result.receipt.kind
                            == ucm::UsbRuntimeConfigReceiptAppliedV9
                        || result.receipt.kind
                            == ucm::UsbRuntimeConfigReceiptSavedStartupV9) {
                        m_runtimeParameterDraft.clear();
                    }
                }
                emit stateChanged();
                refreshNow();
            }, Qt::QueuedConnection);
    connect(this, &DashboardBridge::runtimeActionRequested,
            m_pollWorker, &DashboardPollWorker::runtimeAction,
            Qt::QueuedConnection);
    connect(m_pollWorker, &DashboardPollWorker::runtimeActionCompleted,
            this, [this](const ucm::UsbRuntimeActionResultV9 &result) {
                m_runtimeActionBusy = false;
                m_runtimeActionResult = {
                    {QStringLiteral("success"), result.success},
                    {QStringLiteral("message"), result.message},
                    {QStringLiteral("action"), result.action},
                    {QStringLiteral("result"), result.result},
                    {QStringLiteral("transactionId"), QString::number(result.transactionId)},
                    {QStringLiteral("generation"), QString::number(result.generation)},
                    {QStringLiteral("tareState"), result.tareState},
                    {QStringLiteral("tareGeneration"), result.tareGeneration},
                    {QStringLiteral("faultCode"), result.faultCode}
                };
                emit stateChanged();
                refreshNow();
            }, Qt::QueuedConnection);
    connect(&m_pollThread, &QThread::finished,
            m_pollWorker, &QObject::deleteLater);
    m_pollThread.setObjectName(QStringLiteral("UCM USB latest-only poller"));
    m_pollThread.start();

    m_referenceUiTimer.setSingleShot(true);
    m_referenceUiTimer.setInterval(200);
    connect(&m_referenceForce, &ReferenceForceSource::changed,
            this, &DashboardBridge::queueReferenceUiRefresh);
    connect(&m_referenceUiTimer, &QTimer::timeout,
            this, &DashboardBridge::referenceForceChanged);
    connect(&m_access, &EngineerAccess::changed, this, [this] {
        emit accessModeChanged();
        emit stateChanged();
    });
    clearDiagnostics(QStringLiteral("等待真实遥测"));
    m_timer.setInterval(ucm::ui::telemetryIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this,
            &DashboardBridge::refreshNow);
    m_authorityTimer.setInterval(1000);
    m_authorityTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_authorityTimer, &QTimer::timeout, this,
            &DashboardBridge::serviceControlAuthority);
    m_pollClock.start();
    refreshConnectionState();
    resetConfigurationDraft();
    refreshEvidenceSummary();
    if (m_connected) {
        m_timer.start();
        m_authorityTimer.start();
    }
    if (!m_connected) scheduleReconnect();
    QTimer::singleShot(0, this, &DashboardBridge::refreshNow);
}

DashboardBridge::~DashboardBridge()
{
    m_timer.stop();
    m_authorityTimer.stop();
    m_referenceUiTimer.stop();
    disconnect(&m_referenceForce, nullptr, this, nullptr);
    m_referenceForce.stop();
    m_pollThread.quit();
    m_pollThread.wait(10000);
}

void DashboardBridge::clearMeasurements(const QString &rodState)
{
    m_telemetryReady = false;
    m_totalForceText = QStringLiteral("--");
    m_imbalanceText = QStringLiteral("--");
    m_rodForceTexts = {QStringLiteral("--"), QStringLiteral("--"),
                       QStringLiteral("--"), QStringLiteral("--")};
    m_rodStrainTexts = {QStringLiteral("--"), QStringLiteral("--"),
                        QStringLiteral("--"), QStringLiteral("--")};
    m_rodStateTexts = {rodState, rodState, rodState, rodState};
    m_lastRodForceN = {};
    m_lastForceAvailableMask = 0;
    m_lastFormalTotalN = 0.0;
    m_lastFormalTotalValid = false;
    clearDiagnostics(rodState);
}

QStringList DashboardBridge::referenceForceTexts() const
{
    QStringList result {QStringLiteral("--"), QStringLiteral("--"),
                        QStringLiteral("--"), QStringLiteral("--")};
    if (!m_referenceForce.ready()) return result;
    const ReferenceForceFrame frame = m_referenceForce.latest();
    for (int rod = 0; rod < 4; ++rod) {
        result[rod] = QString::number(frame.forceKn[rod], 'f', 2);
    }
    return result;
}

QStringList DashboardBridge::referenceDifferenceTexts() const
{
    QStringList result {QStringLiteral("等待两路有效值"),
                        QStringLiteral("等待两路有效值"),
                        QStringLiteral("等待两路有效值"),
                        QStringLiteral("等待两路有效值")};
    if (!m_referenceForce.ready()) return result;
    const ReferenceForceFrame frame = m_referenceForce.latest();
    for (int rod = 0; rod < 4; ++rod) {
        if ((m_lastForceAvailableMask & (1U << rod)) == 0U) continue;
        const double ucmKn = m_lastRodForceN[rod] / 1000.0;
        const double differenceKn = ucmKn - frame.forceKn[rod];
        if (frame.forceKn[rod] > 0.0) {
            result[rod] = QStringLiteral("差 %1 kN · %2%")
                .arg(differenceKn, 0, 'f', 2)
                .arg(differenceKn / frame.forceKn[rod] * 100.0,
                     0, 'f', 2);
        } else {
            result[rod] = QStringLiteral("差 %1 kN")
                .arg(differenceKn, 0, 'f', 2);
        }
    }
    return result;
}

QString DashboardBridge::referenceTotalForceText() const
{
    if (!m_referenceForce.ready()) return QStringLiteral("--");
    const ReferenceForceFrame frame = m_referenceForce.latest();
    double total = 0.0;
    for (double force : frame.forceKn) total += force;
    return QString::number(total, 'f', 2);
}

QString DashboardBridge::referenceTotalDifferenceText() const
{
    if (!m_referenceForce.ready()) return QStringLiteral("等待标准力相机");
    if (!m_lastFormalTotalValid) return QStringLiteral("等待ARM正式总力");
    const ReferenceForceFrame frame = m_referenceForce.latest();
    double referenceTotalKn = 0.0;
    for (double force : frame.forceKn) referenceTotalKn += force;
    const double differenceKn = m_lastFormalTotalN / 1000.0
        - referenceTotalKn;
    if (referenceTotalKn <= 0.0) {
        return QStringLiteral("与ARM相差 %1 kN")
            .arg(differenceKn, 0, 'f', 2);
    }
    return QStringLiteral("与ARM相差 %1 kN · %2%")
        .arg(differenceKn, 0, 'f', 2)
        .arg(differenceKn / referenceTotalKn * 100.0, 0, 'f', 2);
}

void DashboardBridge::clearDiagnostics(const QString &state)
{
    m_diagnosticRods.clear();
    for (int rod = 0; rod < 4; ++rod) {
        m_diagnosticRods.push_back(QVariantMap {
            {QStringLiteral("title"),
             QStringLiteral("拉杆 %1 · ADC%2").arg(rod + 1).arg(rod)},
            {QStringLiteral("state"), state},
            {QStringLiteral("force"), QStringLiteral("--")},
            {QStringLiteral("t0"), QStringLiteral("-- / --")},
            {QStringLiteral("ncc"), QStringLiteral("-- / --")},
            {QStringLiteral("lagDelay"), QStringLiteral("-- / --")},
            {QStringLiteral("snrSaturation"), QStringLiteral("-- / --")},
            {QStringLiteral("flags"), QStringLiteral("--")},
            {QStringLiteral("reason"), QStringLiteral("--")}
        });
    }
    m_diagnosticSummary = {
        {QStringLiteral("process"), QStringLiteral("快照未就绪")},
        {QStringLiteral("trust"), QStringLiteral("来源未确认")},
        {QStringLiteral("gate"), state},
        {QStringLiteral("masks"), QStringLiteral("measurement / force / valid / negative = --")},
        {QStringLiteral("frontend"), QStringLiteral("AFE / HV / TGC 回执未就绪")},
        {QStringLiteral("identity"), QStringLiteral("generation / session / frame = --")}
    };
}

void DashboardBridge::clearWaveform(const QString &status)
{
    m_waveformSeries.clear();
    m_waveformGeneration = 0;
    m_waveformStart = 0;
    m_waveformSampleRateHz = 0;
    m_waveformStatus = status;
}

void DashboardBridge::refreshConnectionState()
{
    const bool wasConnected = m_connected;
    const bool firstCheck = !m_connectionStateInitialized;
    m_connectionStateInitialized = true;
    ucm::ControlAuthorityResultV2 authority;
    QMutexLocker lock(&m_sessionMutex);
    const ucm::TransportInfo info = m_session.transportInfo();
    m_usbV2Discovery = m_session.usbExtendedDiscovery();
    m_connected = info.realUsbOpened && info.armReceiverContacted;
    if (m_connected && !m_offlinePreview)
        authority = m_session.readControlAuthorityState();
    m_usbLabel = m_connected
        ? (m_usbV2Discovery.available
            ? QStringLiteral("USB revision 9 已连接")
            : QStringLiteral("USB V1 已连接"))
        : QStringLiteral("USB 待连接");
    if (m_connected && !wasConnected) {
        m_runtimeReady = false;
        m_runtimeStateText = QStringLiteral("等待读取ARM复合运行链状态");
        m_runtimeDetailText.clear();
    }
    lock.unlock();
    if (authority.success) {
        applyControlAuthorityState(authority.state);
        m_controlAuthorityStatus = authority.message;
    } else if (m_connected) {
        m_controlAuthority = {};
        m_controlAuthorityStatus = authority.message;
    }
    if (!m_connected && (wasConnected || firstCheck)) {
        m_product = {};
        ucm::CompoundRuntimeSnapshot snapshot;
        snapshot.authoritativeContractBound = true;
        snapshot.message = QStringLiteral(
            "USB离线，未读取ARM CRS1复合运行状态。");
        const ucm::CompoundRuntimePresentation compound =
            ucm::presentCompoundRuntimeStatus(snapshot);
        m_runtimeReady = compound.runtimeChainReady;
        m_runtimeStateText = compound.stateText;
        m_runtimeDetailText = compound.detailText;
        m_controlAuthority = {};
        m_controlAuthorityStatus = QStringLiteral(
            "USB离线，未读取ARM控制权状态");
        m_processStateText = QStringLiteral("离线");
        m_statusText = QStringLiteral("未找到满足身份和endpoint合同的WinUSB设备。");
        m_eventText = QStringLiteral("等待真实设备握手");
        clearMeasurements(QStringLiteral("等待USB连接"));
        clearWaveform(QStringLiteral("等待USB连接"));
        m_logSourceRecords.clear();
        m_logSources.clear();
        m_logContent.clear();
        m_selectedLogSourceId = 0;
        m_selectedLogTitle = QStringLiteral("尚未选择日志源");
        m_logStatus = QStringLiteral("真实USB / ARM receiver未就绪，未读取日志");
        m_timelineEvents.clear();
        m_timelineStatus = QStringLiteral(
            "真实USB / ARM receiver未就绪，未生成Mock事件");
    }
    if (m_connected != wasConnected || firstCheck) {
        emit connectionChanged();
        emit telemetryChanged();
        emit diagnosticsChanged();
        emit waveformChanged();
    }
}

void DashboardBridge::reconnect()
{
    if (m_offlinePreview) {
        m_statusText = QStringLiteral("离线预览：USB连接已禁用");
        emit telemetryChanged();
        return;
    }
    m_manuallyDisconnected = false;
    ++m_logToken;
    m_logBusy = false;
    if (m_configurationBusy || m_controlModeBusy
        || m_authorityOperationInFlight || m_authorityOperationQueued) {
        m_configurationStatus = QStringLiteral(
            "配置提交/回读正在后台执行；完成前不启动第二个USB事务。") ;
        emit stateChanged();
        return;
    }
    m_trendBuffer.append(QDateTime::currentMSecsSinceEpoch(), 0, {});
    recordCsvGap();
    clearWaveform(QStringLiteral("等待ARM波形快照"));
    m_logSourceRecords.clear();
    m_logSources.clear();
    m_logContent.clear();
    m_selectedLogSourceId = 0;
    m_selectedLogTitle = QStringLiteral("尚未选择日志源");
    m_logStatus = QStringLiteral("重连后请刷新固定日志源");
    m_timelineEvents.clear();
    m_timelineStatus = QStringLiteral("重连后请刷新固定 source 4");
    if (m_pollInFlight) {
        scheduleReconnect();
        return;
    }
    m_pollInFlight = true;
    m_statusText = QStringLiteral("正在后台握手并读取设备能力…");
    emit telemetryChanged();
    emit reconnectRequested();
}

QString DashboardBridge::deviceControlModeText() const
{
    if (!m_controlAuthority.available) return QStringLiteral("未读取");
    return ucm::usbControlModeTextV2(m_controlAuthority.appliedMode);
}

QString DashboardBridge::deviceControlPhaseText() const
{
    if (!m_controlAuthority.available) return QStringLiteral("状态未知");
    return ucm::usbControlPhaseTextV2(m_controlAuthority.phase);
}

QString DashboardBridge::controlLeaseText() const
{
    if (!m_controlAuthority.available) {
        return QStringLiteral("未取得ARM控制权对象；禁止猜测当前控制者");
    }
    if (m_controlAuthority.phase == ucm::kUsbControlPhaseHostActiveV2) {
        return QStringLiteral(
            "Windows每秒续租；失联约5秒后，ARM安全停机成功才恢复自主模式，否则保持故障安全态");
    }
    if (m_controlAuthority.phase == ucm::kUsbControlPhaseHostSafeWaitV2) {
        return QStringLiteral(
            "设备报告旧安全等待阶段，与R2S控制合同不符；仅允许只读");
    }
    if (m_controlAuthority.phase == ucm::kUsbControlPhaseTransitionStoppingV2
        || m_controlAuthority.phase
            == ucm::kUsbControlPhaseTransitionReconfiguringV2) {
        return QStringLiteral("ARM正在安全停发并重建前端控制边界");
    }
    return QStringLiteral(
        "ARM自主闭环运行；USB只读，不接管增益、发射和重找波动作");
}

bool DashboardBridge::canRequestManualControl() const
{
    if (m_offlinePreview || (m_usbV2Discovery.capabilities.activeFeatureMask & ucm::kUsbExtendedFeatureControlAuthorityV2) == 0 || m_productOperationBusy || m_exportBusy) return false;
    return engineerAuthorized() && m_connected
        && m_controlAuthority.available
        && m_controlAuthority.appliedMode
            == ucm::kUsbControlModeAutonomousV2
        && !m_configurationBusy && !m_controlModeBusy
        && !m_authorityOperationInFlight && !m_authorityOperationQueued;
}

bool DashboardBridge::canRequestAutonomousControl() const
{
    if (m_offlinePreview || (m_usbV2Discovery.capabilities.activeFeatureMask & ucm::kUsbExtendedFeatureControlAuthorityV2) == 0 || m_productOperationBusy || m_exportBusy) return false;
    return engineerAuthorized() && m_connected
        && m_controlAuthority.available
        && m_controlAuthority.appliedMode
            == ucm::kUsbControlModeHostManagedV2
        && !m_configurationBusy && !m_controlModeBusy
        && !m_authorityOperationInFlight && !m_authorityOperationQueued;
}

bool DashboardBridge::canResumeManualControl() const
{
    return false; // R2S lease loss returns to AUTONOMOUS or FAULT_SAFE.
}

void DashboardBridge::requestManualControl()
{
    dispatchControlAuthority(ControlAuthorityOperation::SwitchMode,
                             ucm::kUsbControlModeHostManagedV2, true);
}

void DashboardBridge::requestAutonomousControl()
{
    dispatchControlAuthority(ControlAuthorityOperation::SwitchMode,
                             ucm::kUsbControlModeAutonomousV2, true);
}

void DashboardBridge::resumeManualControl()
{
    m_controlAuthorityStatus = QStringLiteral(
        "R2S失联后需在自主模式重新手动接管；不支持旧恢复命令");
    emit stateChanged();
}

void DashboardBridge::dispatchControlAuthority(
    ControlAuthorityOperation operation, quint32 requestedMode,
    bool userInitiated)
{
    const auto reject = [this, userInitiated](const QString &message) {
        if (userInitiated) m_controlModeBusy = false;
        m_controlAuthorityStatus = message;
        emit stateChanged();
    };
    if (m_offlinePreview) { reject(QStringLiteral("离线预览不可发送控制命令")); return; }
    const bool writeOperation = operation != ControlAuthorityOperation::Refresh;
    if (writeOperation && !engineerAuthorized()) {
        reject(QStringLiteral("客户模式只读，未向ARM发送控制权命令"));
        return;
    }
    if (!m_connected || (writeOperation && !m_controlAuthority.available)) {
        reject(QStringLiteral("真实USB或ARM控制权对象未就绪，命令未发送"));
        return;
    }
    if (m_configurationBusy || m_productOperationBusy || m_exportBusy || m_authorityOperationInFlight
        || m_authorityOperationQueued) {
        if (userInitiated) {
            reject(QStringLiteral("已有USB事务正在执行，请等待本次事务闭合"));
        }
        return;
    }
    if (m_pollInFlight) {
        m_authorityOperationQueued = true;
        m_queuedAuthorityOperation = operation;
        m_queuedAuthorityMode = requestedMode;
        m_queuedAuthorityUserInitiated = userInitiated;
        if (userInitiated) {
            m_controlModeBusy = true;
            m_controlAuthorityStatus = QStringLiteral(
                "等待当前遥测读取结束，随后切换ARM控制模式…");
            emit stateChanged();
        }
        return;
    }

    m_authorityOperationInFlight = true;
    m_controlModeBusy = userInitiated
        && (operation == ControlAuthorityOperation::SwitchMode
            || operation == ControlAuthorityOperation::ResumeHost);
    if (m_controlModeBusy) {
        m_controlAuthorityStatus = operation
                == ControlAuthorityOperation::ResumeHost
            ? QStringLiteral("正在恢复USB手动控制并重新启用前端…")
            : QStringLiteral("正在安全切换ARM控制模式…");
    }
    emit stateChanged();
    emit controlAuthorityRequested(operation, requestedMode);
}

void DashboardBridge::finishControlAuthority(
    const ControlAuthorityOperationResult &completed)
{
    m_authorityOperationInFlight = false;
    m_controlModeBusy = false;
    m_productCommissioned = m_connected && completed.productCommissioned;
    if (completed.result.success) {
        applyControlAuthorityState(completed.result.state);
        m_usbV2Discovery = completed.discovery;
        m_controlAuthorityStatus = completed.result.message;
        if (completed.operation == ControlAuthorityOperation::SwitchMode
            || completed.operation == ControlAuthorityOperation::ResumeHost) {
            m_configurationRequiresReconnect = false;
            resetConfigurationDraft();
            m_controlAuthorityStatus =
                QStringLiteral("ARM已进入%1；%2")
                    .arg(deviceControlModeText(),
                         deviceControlPhaseText());
        }
    } else {
        if (completed.result.state.available) {
            applyControlAuthorityState(completed.result.state);
        } else {
            // A failed refresh without a typed state invalidates all cached
            // ownership claims.  Keeping the last successful mode here made
            // the UI say "ARM autonomous" beside status=7/NOT_FOUND.
            m_controlAuthority = {};
            m_usbV2Discovery.capabilities.activeConfigGroupMask = 0U;
            for (ucm::UsbParameterDescriptorV2 &parameter
                 : m_usbV2Discovery.parameters) {
                parameter.accessFlags &=
                    ~ucm::kUsbParameterAccessWriteActiveV2;
            }
        }
        m_controlAuthorityStatus = QStringLiteral("控制权事务失败：%1")
            .arg(completed.result.message);
    }
    if (m_connected && !m_timer.isActive()) m_timer.start();
    if (m_connected && !m_authorityTimer.isActive())
        m_authorityTimer.start();
    emit stateChanged();
    refreshNow();
}

void DashboardBridge::applyControlAuthorityState(
    const ucm::ControlAuthorityStateV2 &state)
{
    m_controlAuthority = state;
}

void DashboardBridge::serviceControlAuthority()
{
    if (m_offlinePreview) return;
    if (!m_connected || m_configurationBusy
        || m_authorityOperationInFlight || m_authorityOperationQueued) {
        return;
    }
    if (engineerAuthorized() && m_controlAuthority.available
        && m_controlAuthority.appliedMode
            == ucm::kUsbControlModeHostManagedV2
        && m_controlAuthority.phase == ucm::kUsbControlPhaseHostActiveV2) {
        dispatchControlAuthority(ControlAuthorityOperation::RenewLease,
                                 ucm::kUsbControlModeHostManagedV2, false);
        return;
    }
    dispatchControlAuthority(ControlAuthorityOperation::Refresh, 0U, false);
}

void DashboardBridge::setActivePage(int page)
{
    if (!m_access.engineerMode() && page != 0 && page != 1 && page != 5)
        return;
    if (m_activePage == page) return;
    m_activePage = page;
    if (m_activePage == ucm::ui::realtimeTrendPage) {
        m_lastWaveformPollMs = m_pollClock.elapsed()
            - ucm::ui::waveformIntervalMs;
        refreshNow();
    }
}

void DashboardBridge::toggleReferenceForce()
{
    if (!m_access.engineerMode()) return;
    if (m_referenceForce.running()) {
        m_referenceForce.stop();
    } else {
        m_referenceForce.start();
    }
    queueReferenceUiRefresh();
}

void DashboardBridge::queueReferenceUiRefresh()
{
    if (!m_referenceUiTimer.isActive()) m_referenceUiTimer.start();
}

void DashboardBridge::refreshNow()
{
    const qint64 now = m_pollClock.elapsed();
    if (!ucm::ui::telemetryDue(m_connected, m_pollInFlight || m_logBusy
        || m_configurationBusy || m_runtimeParameterBusy
        || m_authorityOperationInFlight, now, m_lastTelemetryPollMs)) return;
    m_lastTelemetryPollMs = now;
    const bool pollWaveform = ucm::ui::waveformDue(
        m_activePage, now, m_lastWaveformPollMs);
    if (pollWaveform) m_lastWaveformPollMs = now;
    m_pollInFlight = true;
    emit pollRequested(pollWaveform);
}

bool DashboardBridge::waveformViewportCanRead() const
{
    return m_waveformViewportAvailable && engineerAuthorized()
        && m_connected && !m_offlinePreview && !m_waveformBusy
        && m_waveformGeneration != 0U;
}

void DashboardBridge::captureLatestWaveform()
{
    if (!engineerAuthorized() || m_offlinePreview || !m_connected
        || m_waveformBusy) {
        m_waveformViewportStatus = QStringLiteral(
            "未读取：需要工程师权限、真实USB连接且当前无波形事务。");
        emit waveformChanged();
        return;
    }
    m_waveformBusy = true;
    m_waveformViewportStatus = QStringLiteral(
        "正在读取ARM最近发布的2048点波形…");
    emit waveformChanged();
    emit waveformReadRequested();
}

void DashboardBridge::shiftWaveformViewport(int direction)
{
    if ((direction != -1 && direction != 1)
        || !waveformViewportCanRead()) {
        m_waveformViewportStatus =
            m_waveformViewportAvailable
            ? QStringLiteral("未读取：波形切片读取门尚未全部满足。")
            : QStringLiteral(
                "未读取：当前ARM未发布revision 9波形能力。");
        emit waveformChanged();
        return;
    }
    constexpr int kStep = 2048;
    m_waveformSliceOffset = std::clamp(
        m_waveformSliceOffset + direction * kStep,
        0, waveformSliceMaximum());
    m_waveformBusy = true;
    m_waveformViewportStatus = QStringLiteral(
        "正在读取已保留8192点中的切片偏移%1…")
        .arg(m_waveformSliceOffset);
    emit waveformChanged();
    emit waveformViewportRequested(
        static_cast<quint32>(m_waveformSliceOffset),
        m_waveformGeneration);
}

void DashboardBridge::requestRuntimeAction(int action)
{
    if (action < 1 || action > 3 || m_runtimeActionBusy
        || !engineerAuthorized() || m_offlinePreview || !m_connected
        || !productCanWrite()
        || !m_runtimeProgress.value(QStringLiteral("available")).toBool()) {
        m_runtimeActionResult = {
            {QStringLiteral("success"), false},
            {QStringLiteral("action"), action},
            {QStringLiteral("message"), QStringLiteral(
                 "运行动作要求真实USB、工程权限、HOST_MANAGED写权限和新鲜运行进度。")}
        };
        emit stateChanged();
        return;
    }
    bool ok = false;
    const quint64 generation = m_runtimeProgress
        .value(QStringLiteral("generation")).toString().toULongLong(&ok);
    if (!ok || generation == 0U) {
        m_runtimeActionResult = {{QStringLiteral("success"), false},
            {QStringLiteral("message"), QStringLiteral("运行进度代次无效，未发送动作。")}};
        emit stateChanged();
        return;
    }
    m_runtimeActionBusy = true;
    m_runtimeActionResult = {{QStringLiteral("success"), false},
        {QStringLiteral("action"), action},
        {QStringLiteral("message"), QStringLiteral("等待ARM动作回执…")}};
    emit stateChanged();
    emit runtimeActionRequested(static_cast<quint32>(action), generation);
}

void DashboardBridge::applyPollResult(const DashboardPollResult &result)
{
    m_pollInFlight = false;
    m_waveformViewportAvailable =
        result.diagnosticWaveformCaptureSupported;
    if (m_manuallyDisconnected) return;
    if (m_configurationBusy && m_configurationApplyQueued) {
        m_configurationApplyQueued = false;
        QTimer::singleShot(
            0, this, &DashboardBridge::dispatchPreparedConfiguration);
    } else if (m_authorityOperationQueued) {
        const ControlAuthorityOperation operation =
            m_queuedAuthorityOperation;
        const quint32 requestedMode = m_queuedAuthorityMode;
        const bool userInitiated = m_queuedAuthorityUserInitiated;
        m_authorityOperationQueued = false;
        m_queuedAuthorityUserInitiated = false;
        QTimer::singleShot(0, this, [this, operation, requestedMode,
                                     userInitiated] {
            dispatchControlAuthority(operation, requestedMode,
                                     userInitiated);
        });
    }
    m_productCommissioned = result.productCommissioned;
    m_usbV2Discovery = result.discovery;
    m_product = result.product;
    if (result.runtimeProgress.success) {
        const auto &p = result.runtimeProgress.progress;
        QVariantList currentLna, bestLna;
        for (int lane = 0; lane < 4; ++lane) {
            currentLna.push_back(p.current.lnaDb[lane]);
            bestLna.push_back(p.best.lnaDb[lane]);
        }
        const auto point = [](const ucm::UsbAgcPointV9 &v,
                              const QVariantList &lna) {
            return QVariantMap{{QStringLiteral("lnaDb"), lna},
                {QStringLiteral("pgaDb"), v.pgaDb},
                {QStringLiteral("vcntlCode"), v.vcntlCode},
                {QStringLiteral("nominalHvV"), v.nominalHvV},
                {QStringLiteral("burstCycles"), v.burstCycles}};
        };
        m_runtimeProgress = {
            {QStringLiteral("available"), true},
            {QStringLiteral("generation"), QString::number(p.generation)},
            {QStringLiteral("stage"), p.stage},
            {QStringLiteral("stageText"), ucm::usbRuntimeStageTextV9(p.stage)},
            {QStringLiteral("stageItem"), p.stageItem},
            {QStringLiteral("stageItemCount"), p.stageItemCount},
            {QStringLiteral("overallPermille"), p.overallPermille},
            {QStringLiteral("waitReason"), p.waitReason},
            {QStringLiteral("stageElapsedMs"), QString::number(p.stageElapsedMs)},
            {QStringLiteral("stageLimitMs"), QString::number(p.stageLimitMs)},
            {QStringLiteral("estimatedRemainingMs"), QString::number(p.estimatedRemainingMs)},
            {QStringLiteral("canMeasure"), p.canMeasure},
            {QStringLiteral("qualityLevel"), p.qualityLevel},
            {QStringLiteral("qualityText"), ucm::usbRuntimeQualityTextV9(p.qualityLevel)},
            {QStringLiteral("plcState"), p.plcState},
            {QStringLiteral("plcFresh"), p.plcFresh},
            {QStringLiteral("templateValidMask"), p.templateValidMask},
            {QStringLiteral("tareState"), p.tareState},
            {QStringLiteral("tareGeneration"), p.tareGeneration},
            {QStringLiteral("lastAction"), p.lastAction},
            {QStringLiteral("lastActionResult"), p.lastActionResult},
            {QStringLiteral("faultCode"), p.faultCode},
            {QStringLiteral("activeDeviceModelId"), p.activeDeviceModelId},
            {QStringLiteral("startupDeviceModelId"), p.startupDeviceModelId},
            {QStringLiteral("pendingDeviceModelId"), p.pendingDeviceModelId},
            {QStringLiteral("modelSwitchState"), p.modelSwitchState},
            {QStringLiteral("modelSwitchResult"), p.modelSwitchResult},
            {QStringLiteral("modelProfileOrigin"), p.modelProfileOrigin},
            {QStringLiteral("modelSwitchTransactionId"),
             QString::number(p.modelSwitchTransactionId)},
            {QStringLiteral("current"), point(p.current, currentLna)},
            {QStringLiteral("best"), point(p.best, bestLna)}
        };
    } else if (m_connected) {
        m_runtimeProgress = {{QStringLiteral("available"), false},
                             {QStringLiteral("message"), result.runtimeProgress.message}};
    }
    if (result.runtimeConfig.success) {
        m_runtimeConfig = result.runtimeConfig;
        m_runtimeBurstStatus = result.runtimeConfig.message;
        if (!m_runtimeBurstDraftDirty
            && result.runtimeConfig.receipt.activeConfiguration.txBurstCycles
                >= 1U
            && result.runtimeConfig.receipt.activeConfiguration.txBurstCycles
                <= 8U) {
            m_runtimeBurstDraft = static_cast<int>(
                result.runtimeConfig.receipt.activeConfiguration.txBurstCycles);
        }
    }
    emit stateChanged();
    const bool wasConnected = m_connected;
    m_connected = result.transport.realUsbOpened
        && result.transport.armReceiverContacted;
    m_usbLabel = m_connected
        ? (m_usbV2Discovery.available
            ? QStringLiteral("USB revision 9 已连接")
            : QStringLiteral("USB V1 已连接"))
        : QStringLiteral("USB 待连接");
    const ucm::RuntimeStatusPresentation runtime =
        ucm::presentRuntimeStatus(result.transport, result.runtime);
    const ucm::CompoundRuntimePresentation compound =
        ucm::presentCompoundRuntimeStatus(result.compoundRuntime.snapshot);
    m_runtimeReady = compound.runtimeChainReady;
    m_runtimeStateText = compound.stateText;
    m_runtimeDetailText = compound.detailText;
    if (!m_connected) {
        m_calibrationReadback = {{"busy", false}, {"outcome", "failed"},
            {"message", QStringLiteral("USB离线，当前ARM参数未知")}};
        m_calibration.usbDisconnected();
        m_calibrationValidatedSession.clear();
        m_calibrationAppliedSession.clear();
        if (m_calibrationPendingOperation != 0) {
            m_calibrationOperation = {{QStringLiteral("operation"), m_calibrationPendingOperation},
                {QStringLiteral("outcome"), QStringLiteral("unresolved")},
                {QStringLiteral("message"), QStringLiteral("连接丢失，标定写入结果待对账")},
                {QStringLiteral("busy"), false}};
            m_calibrationPendingOperation = 0;
            m_calibrationPendingSession.clear();
        }
        m_productCommissioned = false;
        if (!m_productOperation.isEmpty()) {
            const bool unresolved = m_productOperationBusy
                || m_productOperation.value(QStringLiteral("outcome")).toString() == QStringLiteral("pending")
                || m_productOperation.value(QStringLiteral("outcome")).toString() == QStringLiteral("unresolved");
            m_productOperation[QStringLiteral("previousEvidence")] = m_productOperation.value(QStringLiteral("fields"));
            m_productOperation[QStringLiteral("fields")] = QVariantMap {};
            m_productOperation[QStringLiteral("busy")] = false;
            if (unresolved) {
                m_productOperation[QStringLiteral("outcome")] = QStringLiteral("unresolved");
                m_productOperation[QStringLiteral("message")] = QStringLiteral("连接已丢失，事务尚未对账；禁止新写入与激活");
            }
            m_productOperationBusy = false;
        }
        m_product = {};
        m_runtimeConfig = {};
        m_runtimeProgress = {};
        m_runtimeActionResult = {};
        m_runtimeActionBusy = false;
        m_runtimeBurstDraftDirty = false;
        m_runtimeBurstStatus = QStringLiteral("USB离线，burst实际值未知");
        m_usbV2Discovery = {};
        m_logSourceRecords.clear();
        m_logSources.clear();
        m_logContent.clear();
        m_timelineEvents.clear();
        m_selectedLogSourceId = 0;
        m_selectedLogTitle = QStringLiteral("尚未选择日志源");
        m_logStatus = QStringLiteral("USB离线，日志快照已清空");
        m_statusText = result.telemetry.message;
        m_trendBuffer.append(QDateTime::currentMSecsSinceEpoch(), 0, {});
    recordCsvGap();
        m_timer.stop();
        m_authorityTimer.stop();
        scheduleReconnect();
        m_controlAuthority = {};
        m_controlAuthorityStatus = QStringLiteral(
            "USB离线；设备当前控制与运行状态未知");
        {
            clearMeasurements(QStringLiteral("等待USB连接"));
            clearWaveform(QStringLiteral("等待USB连接"));
            emit stateChanged();
            emit connectionChanged();
            emit telemetryChanged();
            emit diagnosticsChanged();
            emit waveformChanged();
            queueReferenceUiRefresh();
        }
        return;
    }
    m_reconnectAttempt = 0;
    if (productReadOnly()) {
        if (!m_runtimeBurstBusy)
            m_controlAuthorityStatus = QStringLiteral(
                "revision 9产品连接；自主模式只读，HOST_MANAGED安全停止后可写运行参数");
    }
    if (!wasConnected && !m_timer.isActive()) {
        m_timer.start();
        m_authorityTimer.start();
        emit stateChanged();
        emit connectionChanged();
    }

    const ucm::TelemetrySnapshot &snapshot = result.telemetry;
    if (!snapshot.success) {
        m_processStateText = productReadOnly() ? QStringLiteral("产品结果当前不可用") : runtime.measurementText;
        if (productReadOnly()) {
            m_runtimeReady = false;
            m_runtimeStateText = QStringLiteral("ARM产品结果无效或未配对");
            m_runtimeDetailText = snapshot.message;
        }
        m_statusText = QStringLiteral("%1；当前没有可显示的力值快照：%2")
            .arg(runtime.measurementText, snapshot.message);
        switch (runtime.state) {
        case ucm::RuntimeDisplayState::CapturingNoSnapshot:
            m_eventText = QStringLiteral(
                "50 Hz采集仍在运行；尚未找到回波或生成快照");
            break;
        case ucm::RuntimeDisplayState::CapturingDiagnostic:
            m_eventText = QStringLiteral(
                "50 Hz采集和建模仍在运行；正式力快照尚未通过门限");
            break;
        case ucm::RuntimeDisplayState::SafeWait:
            m_eventText = QStringLiteral(
                "ARM处于配置安全等待；USB连接保持在线");
            break;
        case ucm::RuntimeDisplayState::Reconfiguring:
            m_eventText = QStringLiteral(
                "ARM正在重整活动配置；USB连接保持在线");
            break;
        case ucm::RuntimeDisplayState::Starting:
            m_eventText = QStringLiteral(
                "ARM测量进程正在启动；USB连接保持在线");
            break;
        case ucm::RuntimeDisplayState::RuntimeUnavailable:
            m_eventText = QStringLiteral(
                "USB receiver在线；measurementd运行状态暂不可用");
            break;
        default:
            m_eventText = QStringLiteral(
                "USB连接保持在线；当前没有可显示的力值快照");
            break;
        }
        if (productReadOnly()) { m_statusText = snapshot.message; m_eventText = snapshot.message; }
        clearMeasurements(productReadOnly() ? QStringLiteral("ARM产品结果不可用") : runtime.measurementText);
        m_trendBuffer.append(QDateTime::currentMSecsSinceEpoch(), 0, {});
        recordCsvGap(snapshot.message);
        if (result.waveformRequested) {
            m_waveformSliceOffset = 0;
            applyWaveformSnapshot(result.waveform);
        }
        emit telemetryChanged();
        emit diagnosticsChanged();
        queueReferenceUiRefresh();
        return;
    }

    recordArmSnapshot(snapshot);
    m_calibration.armSample(snapshot, m_product.deviceModel);
    m_telemetryReady = true;
    m_processStateText = snapshot.productResult
        ? (snapshot.formalForceValid
            ? (snapshot.lowLoadBiasInvalid
                ? QStringLiteral("ARM正式力有效 · 低载时偏载率无效")
                : QStringLiteral("ARM正式结果有效"))
            : QStringLiteral("ARM正式结果无效 · 原因%1").arg(snapshot.primaryReasonCode))
        : ucm::normalizedProcessStateText(static_cast<int>(snapshot.processState));
    if (snapshot.productResult) {
        m_runtimeReady = false;
        m_runtimeStateText = QStringLiteral("ARM产品结果已完成身份配对");
        m_runtimeDetailText = QStringLiteral("此状态仅表示结果30与输入45配对；未推断完整运行链");
    }
    m_totalForceText = snapshot.formalForceValid
        ? QString::number(snapshot.formalTotalN, 'f', 2)
        : QStringLiteral("--");
    m_imbalanceText = snapshot.formalForceValid
        && !snapshot.lowLoadBiasInvalid
        ? QString::number(snapshot.imbalanceIndex * 100.0, 'f', 1)
        : QStringLiteral("--");
    m_lastFormalTotalValid = snapshot.formalForceValid;
    m_lastFormalTotalN = snapshot.formalTotalN;
    m_lastForceAvailableMask = snapshot.forceAvailableMask;
    for (int rod = 0; rod < 4; ++rod) {
        const quint32 bit = 1U << rod;
        const bool available = (snapshot.forceAvailableMask & bit) != 0U;
        m_rodForceTexts[rod] = available
            ? QString::number(snapshot.rod[rod].forceN, 'f', 2)
            : QStringLiteral("--");
        m_rodStrainTexts[rod] =
            (snapshot.strainAvailableMask & bit) != 0U
            ? QString::number(snapshot.rod[rod].strainMicrostrain, 'f', 2)
            : QStringLiteral("--");
        m_lastRodForceN[rod] = snapshot.rod[rod].forceN;
        const QString baseState = available
            ? ((snapshot.rodValidMask & bit) != 0U
                ? QStringLiteral("正式杆有效")
                : QStringLiteral("仅诊断可用"))
            : ucm::forceRodReasonText(snapshot.rod[rod].forceReason);
        QStringList quality;
        if ((snapshot.forcePredictionMask & bit) != 0U)
            quality.push_back(QStringLiteral("有限预测"));
        if ((snapshot.forceQualityDegradedMask & bit) != 0U)
            quality.push_back(QStringLiteral("质量降级"));
        if ((snapshot.forceCommonTrendOutlierMask & bit) != 0U)
            quality.push_back(QStringLiteral("共同趋势异常"));
        m_rodStateTexts[rod] = quality.isEmpty()
            ? baseState : baseState + QStringLiteral(" · ")
                + quality.join(QStringLiteral(" · "));
    }
    m_diagnosticRods.clear();
    const QStringList rodAccents {
        QStringLiteral("#F06449"), QStringLiteral("#4D7CFE"),
        QStringLiteral("#2AA198"), QStringLiteral("#8B6FCB")
    };
    for (int rod = 0; rod < 4; ++rod) {
        const quint32 bit = 1U << rod;
        const bool available = (snapshot.forceAvailableMask & bit) != 0U;
        const ucm::RodTelemetrySnapshot &values = snapshot.rod[rod];
        m_diagnosticRods.push_back(QVariantMap {
            {QStringLiteral("title"),
             QStringLiteral("拉杆 %1 · ADC%2").arg(rod + 1).arg(rod)},
            {QStringLiteral("accent"), rodAccents.at(rod)},
            {QStringLiteral("state"), m_rodStateTexts[rod]},
            {QStringLiteral("predicted"),
             (snapshot.forcePredictionMask & bit) != 0U},
            {QStringLiteral("qualityDegraded"),
             (snapshot.forceQualityDegradedMask & bit) != 0U},
            {QStringLiteral("commonTrendOutlier"),
             (snapshot.forceCommonTrendOutlierMask & bit) != 0U},
            {QStringLiteral("force"), available
                ? QString::number(values.forceN, 'f', 2)
                : QStringLiteral("--")},
            {QStringLiteral("strain"),
             (snapshot.strainAvailableMask & bit) != 0U
                ? QString::number(values.strainMicrostrain, 'f', 2)
                : QStringLiteral("--")},
            {QStringLiteral("t0"), QStringLiteral("%1 / %2")
                .arg(values.referenceT0Sample, 0, 'f', 3)
                .arg(values.currentT0Sample, 0, 'f', 3)},
            {QStringLiteral("ncc"), QStringLiteral("%1 / %2")
                .arg(values.nccPeak, 0, 'f', 4)
                .arg(values.nccPeakRatio, 0, 'f', 3)},
            {QStringLiteral("lagDelay"), QStringLiteral("%1 / %2 ns")
                .arg(values.nccLagSamples, 0, 'f', 3)
                .arg(values.delayNs, 0, 'f', 2)},
            {QStringLiteral("snrSaturation"), QStringLiteral("%1 dB / %2")
                .arg(values.snrDb, 0, 'f', 2)
                .arg(values.saturationCount)},
            {QStringLiteral("flags"), hexValue(values.measurementFlags, 8)},
            {QStringLiteral("reason"), QStringLiteral("%1 · %2")
                .arg(values.forceReason)
                .arg(ucm::forceRodReasonText(values.forceReason))}
        });
    }
    m_diagnosticSummary = {
        {QStringLiteral("process"), ucm::normalizedProcessStateText(
            static_cast<int>(snapshot.processState))},
        {QStringLiteral("trust"), snapshot.processStateTrusted
            ? QStringLiteral("状态来源可信") : QStringLiteral("状态来源未可信")},
        {QStringLiteral("gate"), snapshot.formalForceValid
            ? (snapshot.lowLoadBiasInvalid
                ? QStringLiteral("正式力允许；偏载率因总力低于%1 N无效")
                    .arg(snapshot.biasValidMinTotalForceN)
                : QStringLiteral("正式输出允许"))
            : QStringLiteral("正式输出阻断 · %1")
                .arg(ucm::gateReasonText(snapshot.primaryReasonCode))},
        {QStringLiteral("masks"),
         QStringLiteral("measurement %1 · force %2 · valid %3 · negative %4 · prediction %5 · degraded %6 · trend-outlier %7")
            .arg(hexValue(snapshot.measurementValidMask, 1))
            .arg(hexValue(snapshot.forceAvailableMask, 1))
            .arg(hexValue(snapshot.rodValidMask, 1))
            .arg(hexValue(snapshot.negativeForceMask, 1))
            .arg(hexValue(snapshot.forcePredictionMask, 1))
            .arg(hexValue(snapshot.forceQualityDegradedMask, 1))
            .arg(hexValue(snapshot.forceCommonTrendOutlierMask, 1))},
        {QStringLiteral("frontend"),
         QStringLiteral("AFE %1 · receipt %2/%3/%4 · HV %5/%6 · TGC %7 dB · AGC %8(%9) / %10(%11)")
            .arg(hexValue(snapshot.afeProfileCrc32, 8))
            .arg(snapshot.afeReceiptSequence)
            .arg(snapshot.trainingReceiptSequence)
            .arg(snapshot.cadenceReceiptSequence)
            .arg(snapshot.hvSetpoint)
            .arg(snapshot.hvReceiptSequence)
            .arg(snapshot.digitalTgcAttenuationDb)
            .arg(snapshot.agcState)
            .arg(ucm::usbRuntimeAgcStageTextV9(snapshot.agcState))
            .arg(snapshot.agcReason)
            .arg(ucm::usbRuntimeAgcReasonTextV9(snapshot.agcReason))},
        {QStringLiteral("plc"), snapshot.plcStale
            ? QStringLiteral("PLC状态陈旧 · state %1").arg(snapshot.plcState)
            : QStringLiteral("PLC state %1").arg(snapshot.plcState)},
        {QStringLiteral("identity"),
         QStringLiteral("generation %1 · session %2 · seq %3 · frame %4 · window %5+%6")
            .arg(snapshot.generation).arg(snapshot.sessionId)
            .arg(snapshot.sequence).arg(snapshot.frameCounter)
            .arg(snapshot.windowStart).arg(snapshot.windowLength)}
    };
    if (!snapshot.diagnosticFieldsAvailable)
        clearDiagnostics(QStringLiteral("ARM revision 9未发布RESULT_DIAGNOSTICS，应变暂不可用"));
    std::array<double, 4> values {};
    for (int rod = 0; rod < 4; ++rod)
        values[rod] = snapshot.rod[rod].forceN;
    const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_trendBuffer.append(timestampMs, snapshot.forceAvailableMask, values);
    m_statusText = QStringLiteral("真实USB · frame %1 · sequence %2")
        .arg(snapshot.frameCounter).arg(snapshot.sequence);
    m_eventText = snapshot.formalForceValid
        ? (snapshot.lowLoadBiasInvalid
            ? QStringLiteral("正式力有效；总力低于%1 N，偏载率不发布")
                .arg(snapshot.biasValidMinTotalForceN)
            : QStringLiteral("正式力输出已通过有效门"))
        : QStringLiteral("遥测在线，正式力门尚未通过");
    if (result.waveformRequested) {
        m_waveformSliceOffset = 0;
        applyWaveformSnapshot(result.waveform);
    }
    emit telemetryChanged();
    emit diagnosticsChanged();
    queueReferenceUiRefresh();
}

void DashboardBridge::applyWaveformSnapshot(
    const ucm::WaveformSnapshot &snapshot)
{
    if (!snapshot.success) {
        clearWaveform(snapshot.message.contains(QStringLiteral("status=7"))
            ? QStringLiteral("ARM波形快照未就绪（status 7 / NOT_FOUND）")
            : QStringLiteral("波形读取失败"));
        emit waveformChanged();
        return;
    }
    QVariantList series;
    for (int rod = 0; rod < 4; ++rod) {
        QVariantList samples;
        samples.reserve(snapshot.rodSamples[rod].size());
        for (const qint16 value : snapshot.rodSamples[rod])
            samples.push_back(static_cast<int>(value));
        series.push_back(samples);
    }
    m_waveformSeries = series;
    m_waveformGeneration = snapshot.generation;
    m_waveformStart = static_cast<int>(snapshot.windowStart);
    m_waveformSampleRateHz = snapshot.sampleRateHz;
    m_waveformStatus = QStringLiteral("真实USB波形 · frame %1 · 2048×4")
        .arg(snapshot.frameCounter);
    emit waveformChanged();
}

void DashboardBridge::refreshLogSources()
{
    if (!m_connected || m_offlinePreview || m_logBusy || m_configurationBusy) return;
    m_logSourceRecords.clear();
    m_logSources.clear();
    m_logContent.clear();
    m_selectedLogSourceId = 0;
    m_selectedLogTitle = QStringLiteral("尚未选择日志文件");
    m_logStatus = QStringLiteral("正在后台读取设备日志目录…");
    m_logBusy = true;
    emit logSourcesRequested(++m_logToken);
    emit stateChanged();
}

void DashboardBridge::finishLogSources(quint64 token, const ucm::DeviceLogListResult &result)
{
    if (token != m_logToken) return;
    m_logBusy = false;
    if (!m_connected || m_manuallyDisconnected) return;
    if (!result.success) {
        m_logStatus = QStringLiteral("日志目录读取失败：%1").arg(result.message);
        emit stateChanged();
        return;
    }
    m_logSourceRecords = result.sources;
    for (const auto &source : result.sources) {
        m_logSources.push_back(QVariantMap {
            {QStringLiteral("sourceId"), static_cast<int>(source.sourceId)},
            {QStringLiteral("name"), source.name},
            {QStringLiteral("available"), source.available},
            {QStringLiteral("sizeText"), byteSize(source.totalBytes)},
            {QStringLiteral("mutableFile"), source.mutableFile},
            {QStringLiteral("identityCrc"), hexValue(source.fileIdentityCrc32, 8)},
            {QStringLiteral("state"), !source.available ? QStringLiteral("不可用") :
                (source.mutableFile ? QStringLiteral("持续写入 · 可变") : QStringLiteral("已封存 · 不可变"))}
        });
    }
    m_logStatus = QStringLiteral("设备返回 %1 个日志文件；选中后读取首块预览，完整导出需校验hash")
        .arg(result.sources.size());
    emit stateChanged();
}

void DashboardBridge::loadLogSource(int sourceId)
{
    if (!m_connected || m_offlinePreview || m_logBusy || m_configurationBusy) return;
    const auto selected = std::find_if(m_logSourceRecords.cbegin(), m_logSourceRecords.cend(),
        [sourceId](const ucm::DeviceLogSource &source) { return source.sourceId == static_cast<quint32>(sourceId); });
    if (selected == m_logSourceRecords.cend() || !selected->available) return;
    m_selectedLogSourceId = sourceId;
    m_selectedLogTitle = selected->name;
    m_logContent.clear();
    m_logStatus = QStringLiteral("正在后台读取首块（最多4 KiB）…");
    m_logBusy = true;
    emit logChunkRequested(++m_logToken, selected->sourceId, selected->snapshotId);
    emit stateChanged();
}

void DashboardBridge::finishLogChunk(quint64 token, const ucm::DeviceLogChunkResult &result)
{
    if (token != m_logToken) return;
    m_logBusy = false;
    if (!m_connected || m_manuallyDisconnected) return;
    if (!result.success) {
        m_logContent.clear();
        m_logStatus = QStringLiteral("日志首块读取失败：%1").arg(result.message);
        if (productReadOnly()) QTimer::singleShot(0, this, &DashboardBridge::refreshLogSources);
    } else {
        // URS is binary. Never interpret a binary product log as UTF-8 text.
        if (productReadOnly()) {
            m_logContent = QStringLiteral("二进制首块十六进制预览（非全文，未完成整文件hash校验）\n");
            for (qsizetype offset = 0; offset < result.data.size(); offset += 16)
                m_logContent += QStringLiteral("%1  %2\n")
                    .arg(offset, 8, 16, QLatin1Char('0'))
                    .arg(QString::fromLatin1(result.data.mid(offset, 16).toHex(' ')));
        } else {
            m_logContent = QString::fromUtf8(result.data);
        }
        m_logStatus = QStringLiteral("%1 · 首块 %2 / %3 字节 · snapshot %4 · identity %5")
            .arg(m_selectedLogTitle).arg(result.data.size()).arg(result.totalBytes)
            .arg(result.snapshotId).arg(hexValue(result.fileIdentityCrc32, 8));
    }
    emit stateChanged();
}

void DashboardBridge::refreshTimeline()
{
    if (productReadOnly() || m_offlinePreview || !m_connected) {
        m_timelineEvents.clear();
        m_timelineStatus = QStringLiteral("产品事件文本解码尚未开放；可从诊断包读取原始日志");
        emit stateChanged();
        return;
    }
    if (m_configurationBusy) {
        m_timelineStatus = QStringLiteral(
            "配置提交/回读进行中；完成后再读取事件时间线。") ;
        emit stateChanged();
        return;
    }
    refreshConnectionState();
    m_timelineEvents.clear();
    if (!m_connected) {
        m_timelineStatus = QStringLiteral(
            "真实USB / ARM receiver未就绪，未使用本地文件或Mock生成事件");
        emit stateChanged();
        return;
    }

    ucm::DeviceLogListResult list;
    {
        QMutexLocker lock(&m_sessionMutex);
        list = m_session.readLogSources();
    }
    if (!list.success) {
        m_timelineStatus = QStringLiteral("日志清单读取失败：%1")
            .arg(list.message);
        emit stateChanged();
        return;
    }
    const auto source = std::find_if(
        list.sources.cbegin(), list.sources.cend(),
        [](const ucm::DeviceLogSource &candidate) {
            return candidate.sourceId == 4U;
        });
    if (source == list.sources.cend() || !source->available) {
        m_timelineStatus = QStringLiteral(
            "固定 R3 diagnostics 源（source 4）当前不可用");
        emit stateChanged();
        return;
    }
    if (source->totalBytes == 0U) {
        m_timelineStatus = QStringLiteral("R3 diagnostics 日志为空");
        emit stateChanged();
        return;
    }

    constexpr quint64 maximumBytes = 65536U;
    const quint64 offset = source->totalBytes > maximumBytes
        ? source->totalBytes - maximumBytes : 0U;
    const quint32 wanted = static_cast<quint32>(
        source->totalBytes - offset);
    ucm::DeviceLogChunkResult chunk;
    {
        QMutexLocker lock(&m_sessionMutex);
        chunk = m_session.readLogChunk(
            4U, offset, wanted, source->snapshotId);
    }
    if (!chunk.success) {
        m_timelineStatus = QStringLiteral("R3 diagnostics 读取失败：%1")
            .arg(chunk.message);
        emit stateChanged();
        return;
    }

    const ucm::R3EventParseResult parsed = ucm::parseR3EventCsv(
        chunk.data, offset == 0U);
    if (!parsed.success) {
        m_timelineStatus = QStringLiteral("R3 冻结契约校验失败：%1")
            .arg(parsed.message);
        emit stateChanged();
        return;
    }

    const QStringList severityAccents {
        QStringLiteral("#2AA198"), QStringLiteral("#4D7CFE"),
        QStringLiteral("#F0A84B"), QStringLiteral("#D84F4F")
    };
    for (qsizetype index = parsed.records.size(); index > 0; --index) {
        const ucm::R3EventRecord &event = parsed.records.at(index - 1);
        const QString process = event.processState < 0
            ? QStringLiteral("未提供")
            : ucm::normalizedProcessStateText(event.processState);
        const QString formal = event.formalEligible < 0
            ? QStringLiteral("未提供")
            : (event.formalEligible != 0
                ? QStringLiteral("允许") : QStringLiteral("阻断"));
        const QString agc = event.agcLifecycle < 0
            ? QStringLiteral("未提供")
            : ucm::agcLifecycleText(event.agcLifecycle);
        m_timelineEvents.push_back(QVariantMap {
            {QStringLiteral("accent"), severityAccents.at(event.severity)},
            {QStringLiteral("lifecycle"),
             ucm::eventLifecycleText(event.lifecycle)},
            {QStringLiteral("identity"),
             QStringLiteral("ID %1 · seq %2\nframe %3")
                .arg(event.eventId).arg(event.eventSequence)
                .arg(event.frameCounter)},
            {QStringLiteral("timestamp"),
             QStringLiteral("%1 ns").arg(event.eventTimestampNs)},
            {QStringLiteral("severity"), ucm::severityText(event.severity)},
            {QStringLiteral("reason"),
             QStringLiteral("%1 [%2]\n%3 [%4]")
                .arg(ucm::reasonFamilyText(event.reasonFamily))
                .arg(event.reasonFamily)
                .arg(ucm::diagnosticReasonText(event.reasonCode))
                .arg(event.reasonCode)},
            {QStringLiteral("customerReason"),
             QStringLiteral("%1 · %2")
                .arg(ucm::reasonFamilyText(event.reasonFamily),
                     ucm::diagnosticReasonText(event.reasonCode))},
            {QStringLiteral("stageScope"),
             QStringLiteral("%1 [%2]\n%3 [%4]")
                .arg(ucm::stageText(event.stage)).arg(event.stage)
                .arg(ucm::scopeText(event.scope)).arg(event.scope)},
            {QStringLiteral("process"), process},
            {QStringLiteral("formalAgc"),
             QStringLiteral("正式：%1\nAGC：%2").arg(formal, agc)}
        });
    }
    m_timelineStatus = QStringLiteral(
        "%1 · source 4 · %2 B · snapshot %3 · identity %4%5")
        .arg(parsed.message).arg(chunk.data.size()).arg(chunk.snapshotId)
        .arg(hexValue(chunk.fileIdentityCrc32, 8))
        .arg(parsed.discardedPartialFirstLine
            ? QStringLiteral(" · 已丢弃尾部窗口首个半行") : QString());
    emit stateChanged();
}

QVariantList DashboardBridge::afeConfigurationFields() const
{
    const ucm::Configuration &candidate = m_session.candidate();
    QVariantList lnaOptions;
    for (const int value : {12, 18, 24}) {
        lnaOptions.push_back(QVariantMap {
            {QStringLiteral("label"), QStringLiteral("%1 dB").arg(value)},
            {QStringLiteral("value"), value}
        });
    }

    QVariantList digitalGainOptions;
    for (int value = 0; value <= 30; ++value) {
        digitalGainOptions.push_back(QVariantMap {
            {QStringLiteral("label"), QStringLiteral("%1 步").arg(value)},
            {QStringLiteral("value"), value}
        });
    }

    QVariantList tgcOptions;
    for (const int value : {0, 6, 12, 18, 24, 30, 36, 42}) {
        tgcOptions.push_back(QVariantMap {
            {QStringLiteral("label"), value == 42
                 ? QStringLiteral("42 dB · 最大衰减")
                 : QStringLiteral("%1 dB").arg(value)},
            {QStringLiteral("value"), value}
        });
    }

    QVariantList fields;
    for (int rod = 0; rod < 4; ++rod) {
        fields.push_back(QVariantMap {
            {QStringLiteral("key"),
             QStringLiteral("lna_gain_db_rod_%1").arg(rod + 1)},
            {QStringLiteral("label"),
             QStringLiteral("杆%1 LNA增益").arg(rod + 1)},
            {QStringLiteral("note"), QStringLiteral("各杆测量状态")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("value"), candidate.afe.lnaGainDbByRod[rod]},
            {QStringLiteral("options"), lnaOptions}
        });
        fields.push_back(QVariantMap {
            {QStringLiteral("key"),
             QStringLiteral("digital_gain_steps_rod_%1").arg(rod + 1)},
            {QStringLiteral("label"),
             QStringLiteral("杆%1 数字增益").arg(rod + 1)},
            {QStringLiteral("note"), QStringLiteral("0–30步，逐杆控制")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("value"),
             candidate.afe.digitalGainStepsByRod[rod]},
            {QStringLiteral("options"), digitalGainOptions}
        });
    }
    fields.push_back(QVariantMap {
            {QStringLiteral("key"), QStringLiteral("digital_tgc_attenuation_db")},
            {QStringLiteral("label"), QStringLiteral("数字TGC衰减")},
            {QStringLiteral("note"), QStringLiteral("四杆公共，硬件步进6 dB")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("value"), candidate.afe.digitalTgcAttenuationDb},
            {QStringLiteral("options"), tgcOptions}
        });
    return fields;
}

QVariantList DashboardBridge::algorithmConfigurationFields() const
{
    const ucm::AlgorithmConfig &algorithm = m_session.candidate().algorithm;
    constexpr int maximumInteger = std::numeric_limits<int>::max();
    const auto integerField = [](const QString &key, const QString &label,
                                 const QString &note, int value, int minimum,
                                 int maximum, const QString &unit) {
        return QVariantMap {
            {QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("note"), note},
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("value"), value}, {QStringLiteral("min"), minimum},
            {QStringLiteral("max"), maximum}, {QStringLiteral("unit"), unit}
        };
    };
    const auto decimalField = [](const QString &key, const QString &label,
                                 const QString &note, double value,
                                 double minimum, double maximum,
                                 int decimals, const QString &unit) {
        return QVariantMap {
            {QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("note"), note},
            {QStringLiteral("type"), QStringLiteral("decimal")},
            {QStringLiteral("value"), value}, {QStringLiteral("min"), minimum},
            {QStringLiteral("max"), maximum},
            {QStringLiteral("decimals"), decimals},
            {QStringLiteral("unit"), unit}
        };
    };

    return {
        integerField(QStringLiteral("template_confirm_frames"),
                     QStringLiteral("模板确认帧数"),
                     QStringLiteral("运行中建模稳定确认"),
                     algorithm.templateConfirmFrames, 1, maximumInteger,
                     QStringLiteral("帧")),
        decimalField(QStringLiteral("ncc_peak_threshold"),
                     QStringLiteral("NCC峰值阈值"),
                     QStringLiteral("相关质量有效门"),
                     algorithm.nccPeakThreshold, 0.0, 1.0, 3, QString()),
        integerField(QStringLiteral("rod_length_mm"),
                     QStringLiteral("拉杆总长"),
                     QStringLiteral("由实际采集地址上限约束"),
                     algorithm.rodLengthMm, 1,
                     ucm::kMaximumAcousticRodLengthMm,
                     QStringLiteral("mm")),
        integerField(QStringLiteral("measurement_point_mm"),
                     QStringLiteral("测点位置"),
                     QStringLiteral("必须位于拉杆长度内"),
                     algorithm.measurementPointMm, 0, algorithm.rodLengthMm,
                     QStringLiteral("mm")),
        integerField(QStringLiteral("minimum_valid_force_n"),
                     QStringLiteral("最小有效总力"),
                     QStringLiteral("正式总力发布门"),
                     algorithm.minimumValidForceN, 0, maximumInteger,
                     QStringLiteral("N")),
        decimalField(QStringLiteral("imbalance_alarm_threshold"),
                     QStringLiteral("偏载率报警阈值"),
                     QStringLiteral("0.20代表20%"),
                     algorithm.imbalanceAlarmThreshold, 0.0, 1.0, 3,
                     QString()),
        integerField(QStringLiteral("consecutive_alarm_frames"),
                     QStringLiteral("连续报警帧数"),
                     QStringLiteral("抑制单帧误报警"),
                     algorithm.consecutiveAlarmFrames, 1, maximumInteger,
                     QStringLiteral("帧"))
    };
}

QVariantList DashboardBridge::lockedConfigurationFields() const
{
    const ucm::Configuration &candidate = m_session.candidate();
    return {
        QVariantMap {{QStringLiteral("label"), QStringLiteral("对象 / Profile")},
                     {QStringLiteral("value"), QStringLiteral("%1 / %2")
                          .arg(candidate.objectId, candidate.deviceProfile)},
                     {QStringLiteral("note"), QStringLiteral("身份字段，不允许自由写")}},
        QVariantMap {{QStringLiteral("label"), QStringLiteral("中心频率")},
                     {QStringLiteral("value"), QStringLiteral("2500 kHz")},
                     {QStringLiteral("note"), QStringLiteral("当前换能器产品固定")}},
        QVariantMap {{QStringLiteral("label"), QStringLiteral("PGA / VCNTL")},
                     {QStringLiteral("value"), QStringLiteral("%1 dB / DAC %2")
                          .arg(candidate.afe.pgaGainDb)
                          .arg(candidate.afe.vcntlDac)},
                     {QStringLiteral("note"), QStringLiteral("V2当前只读；不再由旧联合增益控件误改")}},
        QVariantMap {{QStringLiteral("label"), QStringLiteral("前端保护")},
                     {QStringLiteral("value"), QStringLiteral("低频抑制 开 · 数字高通 关 · 输入钳位 开")},
                     {QStringLiteral("note"), QStringLiteral("安全约束")}},
        QVariantMap {{QStringLiteral("label"), QStringLiteral("四杆物理映射")},
                     {QStringLiteral("value"), QStringLiteral("杆1→ADC0 · 杆2→ADC1 · 杆3→ADC2 · 杆4→ADC3")},
                     {QStringLiteral("note"), QStringLiteral("现场接线合同，禁止在此页改序")}},
        QVariantMap {{QStringLiteral("label"), QStringLiteral("配置版本")},
                     {QStringLiteral("value"), QStringLiteral("活动 %1 · 候选 %2")
                          .arg(m_session.active().cfgVersion,
                               candidate.cfgVersion)},
                     {QStringLiteral("note"), QStringLiteral("准备时自动连续递增")}}
    };
}

QVariantList DashboardBridge::usbV2SummaryItems() const
{
    const auto item = [](const QString &label, const QString &value,
                         const QString &note, bool active) {
        return QVariantMap {
            {QStringLiteral("label"), label},
            {QStringLiteral("value"), value},
            {QStringLiteral("note"), note},
            {QStringLiteral("active"), active}
        };
    };
    if (!m_usbV2Discovery.available) {
        return {item(QStringLiteral("USB V2扩展"),
                     QStringLiteral("未发现"),
                     m_usbV2Discovery.message, false)};
    }

    const ucm::UsbExtendedCapabilitiesV2 &capabilities =
        m_usbV2Discovery.capabilities;
    const bool configActive =
        (capabilities.activeFeatureMask
         & ucm::kUsbExtendedFeatureConfigV2) != 0U
        && capabilities.activeConfigGroupMask != 0U;
    const bool upgradeActive = capabilities.activeUpgradeTargetMask != 0U;
    return {
        item(QStringLiteral("协议身份"),
             QStringLiteral("Schema %1 · Rev %2")
                 .arg(capabilities.schemaVersion)
                 .arg(capabilities.protocolRevision),
             QStringLiteral("%1 · %2")
                 .arg(capabilities.deviceClass, capabilities.buildId), true),
        item(QStringLiteral("参数目录"),
             m_usbV2Discovery.catalogReady
                 ? QStringLiteral("%1 项已校验")
                       .arg(m_usbV2Discovery.parameters.size())
                 : QStringLiteral("未闭合"),
             QStringLiteral("CRC32 0x%1")
                 .arg(capabilities.parameterCatalogCrc32, 8, 16,
                      QLatin1Char('0')).toUpper(),
             m_usbV2Discovery.catalogReady),
        item(QStringLiteral("传输边界"),
             QStringLiteral("%1 KiB分块 · 单请求")
                 .arg(capabilities.recommendedChunkBytes / 1024U),
             QStringLiteral("单帧最大 %1 KiB · 同一WinUSB接口")
                 .arg(capabilities.maximumPayloadBytes / 1024U), true),
        item(QStringLiteral("扩展配置V2"),
             configActive ? QStringLiteral("已激活")
                          : QStringLiteral("未激活"),
             configActive
                 ? QStringLiteral("仅按设备active分组开放")
                  : QStringLiteral("自主模式只读；本版本绝不回退V1配置"),
             configActive),
        item(QStringLiteral("固件/资产升级"),
             upgradeActive ? QStringLiteral("已激活")
                           : QStringLiteral("安全锁定"),
             upgradeActive
                 ? QStringLiteral("仍需逐目标安全态与回滚检查")
                 : QStringLiteral("ARM / FPGA / STM32均不允许从本界面写入"),
             upgradeActive),
        item(QStringLiteral("AFE粒度"),
             QStringLiteral("全局 + 逐杆 + 资产级"),
             QStringLiteral("PGA/VCNTL/TGC全局；LNA/数字增益逐杆；静态项资产级"),
             m_usbV2Discovery.catalogReady)
    };
}

QVariantList DashboardBridge::usbV2ParameterFields() const
{
    QVariantList result;
    result.reserve(m_usbV2Discovery.parameters.size());
    for (const ucm::UsbParameterDescriptorV2 &parameter
         : m_usbV2Discovery.parameters) {
        const bool writeActive = ucm::usbParameterEffectivelyWritableV2(
            parameter,
            m_usbV2Discovery.capabilities.activeConfigGroupMask);
        result.push_back(QVariantMap {
            {QStringLiteral("fieldId"), static_cast<int>(parameter.fieldId)},
            {QStringLiteral("label"),
             ucm::usbParameterFieldNameV2(parameter.fieldId)},
            {QStringLiteral("group"),
             ucm::usbParameterGroupNameV2(parameter.groupId)},
            {QStringLiteral("scope"),
             ucm::usbParameterScopeNameV2(parameter.scope)},
            {QStringLiteral("kind"),
             ucm::usbParameterValueKindNameV2(parameter.valueKind)},
            {QStringLiteral("access"),
             writeActive
                 ? QStringLiteral("可写 · 当前控制权已开放")
                 : ucm::usbParameterAccessTextV2(parameter.accessFlags)},
            {QStringLiteral("writeChannel"),
             ucm::usbParameterWriteChannelTextV2(parameter.fieldId)},
            {QStringLiteral("constraint"),
             ucm::usbParameterConstraintTextV2(parameter)},
            {QStringLiteral("writeActive"), writeActive}
        });
    }
    return result;
}

QVariantList DashboardBridge::writableConfigurationFields() const
{
    QVariantList result;
    const auto localCalibration = [&](int fieldId, const QString &label,
                                      const QString &constraint) {
        result.push_back(QVariantMap {
            {QStringLiteral("fieldId"), fieldId},
            {QStringLiteral("label"), label},
            {QStringLiteral("group"), QStringLiteral("标定")},
            {QStringLiteral("scope"), fieldId == 33
                ? QStringLiteral("逐杆") : QStringLiteral("全局")},
            {QStringLiteral("kind"), fieldId == 33
                ? QStringLiteral("F64[4]") : QStringLiteral("F64")},
            {QStringLiteral("access"), productCanWrite()
                ? QStringLiteral("可写 · 型号文档已开放")
                : QStringLiteral("可配置 · 当前安全门未开放")},
            {QStringLiteral("writeChannel"),
             QStringLiteral("DeviceModel schema 2 · Apply/SaveStartup分离")},
            {QStringLiteral("constraint"), constraint},
            {QStringLiteral("writeActive"), productCanWrite()},
            {QStringLiteral("provenance"),
             QStringLiteral("型号文档字段；不属于msg17的64项wire目录")}
        });
    };
    localCalibration(32, QStringLiteral("统一材料标定系数 kmat_unified"),
                     QStringLiteral("有限正数 · ARM校验标定资格"));
    localCalibration(33, QStringLiteral("四路耦合偏置 coupling_bias_ns"),
                     QStringLiteral("4个有限数 · 单位ns"));

    const bool runtimeSafe = ucm::usbRuntimeHardwareSafeForWriteV9(
        m_runtimeConfig.receipt.hardwareFlags)
        && ucm::usbHostManagedStoppedV2(m_controlAuthority);
    for (const ucm::UsbParameterDescriptorV2 &parameter
         : m_usbV2Discovery.parameters) {
        if ((parameter.accessFlags & ucm::kUsbParameterAccessWriteSupportedV2) == 0U
            || ucm::usbParameterWriteChannelV2(parameter.fieldId)
                == ucm::UsbParameterWriteChannelV2::ReadOnlyRuntime
            || (parameter.accessFlags & ucm::kUsbParameterAccessFrozenV2) != 0U) {
            continue;
        }
        const bool catalogActive = ucm::usbParameterEffectivelyWritableV2(
            parameter,
            m_usbV2Discovery.capabilities.activeConfigGroupMask);
        const bool deviceModel = ucm::usbParameterWriteChannelV2(parameter.fieldId)
            == ucm::UsbParameterWriteChannelV2::DeviceModelDocument;
        const bool writeActive = catalogActive
            && (deviceModel ? productCanWrite() : runtimeSafe);
        result.push_back(QVariantMap {
            {QStringLiteral("fieldId"), static_cast<int>(parameter.fieldId)},
            {QStringLiteral("label"), ucm::usbParameterFieldNameV2(parameter.fieldId)},
            {QStringLiteral("group"), ucm::usbParameterGroupNameV2(parameter.groupId)},
            {QStringLiteral("scope"), ucm::usbParameterScopeNameV2(parameter.scope)},
            {QStringLiteral("kind"), ucm::usbParameterValueKindNameV2(parameter.valueKind)},
            {QStringLiteral("access"), writeActive
                ? QStringLiteral("可写 · 当前安全门已开放")
                : QStringLiteral("可配置 · 当前只读")},
            {QStringLiteral("writeChannel"),
             ucm::usbParameterWriteChannelTextV2(parameter.fieldId)},
            {QStringLiteral("constraint"),
             ucm::usbParameterConstraintTextV2(parameter)},
            {QStringLiteral("writeActive"), writeActive},
            {QStringLiteral("provenance"), QStringLiteral("msg17 canonical catalog")}
        });
    }
    return result;
}

QVariantMap DashboardBridge::runtimeBurstControl() const
{
    const bool actualValid = m_runtimeConfig.success
        && (m_runtimeConfig.receipt.hardwareFlags
            & ucm::kUsbRuntimeHardwareStateValidV9) != 0U;
    bool catalogWritable = false;
    for (const auto &parameter : m_usbV2Discovery.parameters) {
        if (parameter.fieldId == ucm::UsbParameterTxBurstCyclesV2) {
            catalogWritable = ucm::usbParameterEffectivelyWritableV2(
                parameter,
                m_usbV2Discovery.capabilities.activeConfigGroupMask);
            break;
        }
    }
    const bool unloaded = actualValid
        && ucm::usbRuntimeHardwareSafeForWriteV9(
            m_runtimeConfig.receipt.hardwareFlags);
    const bool canWrite = engineerAuthorized() && m_connected
        && !m_offlinePreview && !m_runtimeBurstBusy
        && !m_runtimeParameterBusy
        && !m_productOperationBusy && !m_configurationBusy
        && !m_controlModeBusy && !m_authorityOperationInFlight
        && !m_authorityOperationQueued && catalogWritable
        && ucm::usbHostManagedStoppedV2(m_controlAuthority) && unloaded;
    return {
        {QStringLiteral("valid"), actualValid},
        {QStringLiteral("configured"), m_runtimeConfig.success
            ? static_cast<int>(m_runtimeConfig.receipt.activeConfiguration.txBurstCycles) : 0},
        {QStringLiteral("actual"), actualValid
            ? static_cast<int>(m_runtimeConfig.receipt.actualTxBurstCycles) : 0},
        {QStringLiteral("agcStage"), actualValid
            ? static_cast<int>(m_runtimeConfig.receipt.agcStage) : 0},
        {QStringLiteral("agcReason"), actualValid
            ? static_cast<int>(m_runtimeConfig.receipt.agcReason) : 0},
        {QStringLiteral("agcStatus"), actualValid
            ? QStringLiteral("%1 · %2")
                  .arg(ucm::usbRuntimeAgcStageTextV9(
                      m_runtimeConfig.receipt.agcStage))
                  .arg(ucm::usbRuntimeAgcReasonTextV9(
                      m_runtimeConfig.receipt.agcReason))
            : QStringLiteral("AGC状态尚未读取")},
        {QStringLiteral("draft"), m_runtimeBurstDraft},
        {QStringLiteral("dirty"), m_runtimeBurstDraftDirty},
        {QStringLiteral("canWrite"), canWrite},
        {QStringLiteral("busy"), m_runtimeBurstBusy},
        {QStringLiteral("readOnly"), !canWrite},
        {QStringLiteral("status"), !actualValid
            ? QStringLiteral("等待读取ARM硬件状态；当前只读。")
            : !unloaded
                ? QStringLiteral("设备处于受力/加载态；burst保持只读，确认卸载后才能写。")
                : m_runtimeBurstStatus}
    };
}

void DashboardBridge::setRuntimeBurstDraft(int cycles)
{
    if (cycles < 1 || cycles > 8 || m_runtimeBurstBusy) return;
    m_runtimeBurstDraft = cycles;
    m_runtimeBurstDraftDirty = !m_runtimeConfig.success
        || static_cast<quint32>(cycles)
            != m_runtimeConfig.receipt.activeConfiguration.txBurstCycles;
    m_runtimeBurstStatus = QStringLiteral("burst候选为%1周期；尚未发送。").arg(cycles);
    emit stateChanged();
}

void DashboardBridge::applyRuntimeBurst(bool authorized)
{
    const QVariantMap state = runtimeBurstControl();
    if (!authorized || !engineerAuthorized()
        || !state.value(QStringLiteral("canWrite")).toBool()) {
        m_runtimeBurstStatus = QStringLiteral(
            "未发送：需要工程师登录、HOST_MANAGED、有效租约、硬件安全停止和UHW2确认卸载。");
        emit stateChanged();
        return;
    }
    m_runtimeBurstBusy = true;
    m_runtimeBurstStatus = QStringLiteral("正在应用burst=%1并核对硬件实际值…")
        .arg(m_runtimeBurstDraft);
    emit stateChanged();
    emit runtimeBurstOperationRequested(
        ucm::UsbRuntimeConfigApplyV9, m_runtimeBurstDraft);
}

void DashboardBridge::saveRuntimeBurstStartup(bool authorized)
{
    const QVariantMap state = runtimeBurstControl();
    if (!authorized || !engineerAuthorized()
        || !state.value(QStringLiteral("canWrite")).toBool()
        || m_runtimeBurstDraftDirty) {
        m_runtimeBurstStatus = m_runtimeBurstDraftDirty
            ? QStringLiteral("请先应用并回读当前burst候选，再保存为开机配置。")
            : QStringLiteral("未发送：当前没有运行配置持久化权限。");
        emit stateChanged();
        return;
    }
    m_runtimeBurstBusy = true;
    m_runtimeBurstStatus = QStringLiteral("正在把ARM当前活动配置保存到A/B启动槽…");
    emit stateChanged();
    emit runtimeBurstOperationRequested(
        ucm::UsbRuntimeConfigSaveStartupV9, m_runtimeBurstDraft);
}

bool DashboardBridge::runtimeConfigurationCanWrite() const
{
    return engineerAuthorized() && m_connected && !m_offlinePreview
        && !m_runtimeParameterBusy && !m_runtimeBurstBusy
        && !m_productOperationBusy && !m_configurationBusy
        && !m_controlModeBusy && !m_authorityOperationInFlight
        && !m_authorityOperationQueued && m_usbV2Discovery.catalogReady
        && m_runtimeConfig.success
        && ucm::usbHostManagedStoppedV2(m_controlAuthority)
        && ucm::usbRuntimeHardwareSafeForWriteV9(
            m_runtimeConfig.receipt.hardwareFlags);
}

QVariantList DashboardBridge::runtimeConfigurationFields() const
{
    QVariantList fields;
    if (!m_runtimeConfig.success) return fields;
    const auto &active = m_runtimeConfig.receipt.activeConfiguration;
    for (const auto &descriptor : m_usbV2Discovery.parameters) {
        if (ucm::usbParameterWriteChannelV2(descriptor.fieldId)
                != ucm::UsbParameterWriteChannelV2::RuntimeConfiguration
            || !ucm::usbParameterEffectivelyWritableV2(
                descriptor,
                m_usbV2Discovery.capabilities.activeConfigGroupMask)
            || (descriptor.accessFlags
                & ucm::kUsbParameterAccessFrozenV2) != 0U
            || descriptor.fieldId == 17U || descriptor.fieldId == 20U
            || descriptor.fieldId == ucm::UsbParameterTxBurstCyclesV2) {
            continue;
        }
        const int count = descriptor.scope == 2U ? 4 : 1;
        for (int item = 0; item < count; ++item) {
            const int elementIndex = descriptor.scope == 2U ? item : -1;
            qint64 current = 0;
            if (!ucm::usbRuntimeParameterValueV9(
                    active, descriptor.fieldId, elementIndex, &current)) {
                continue;
            }
            const QString key = QStringLiteral("%1:%2")
                .arg(descriptor.fieldId).arg(elementIndex);
            const qint64 draft = m_runtimeParameterDraft.value(key, current);
            QVariantList allowedValues;
            if ((descriptor.constraintFlags
                 & ucm::kUsbParameterConstraintEnumMaskV2) != 0U) {
                for (int value = 0; value < 64; ++value) {
                    if ((descriptor.enumMask & (1ULL << value)) != 0U)
                        allowedValues.push_back(value);
                }
            }
            fields.push_back(QVariantMap {
                {QStringLiteral("fieldId"), descriptor.fieldId},
                {QStringLiteral("elementIndex"), elementIndex},
                {QStringLiteral("label"), descriptor.scope == 2U
                    ? QStringLiteral("%1 · 杆%2")
                        .arg(ucm::usbParameterFieldNameV2(descriptor.fieldId))
                        .arg(item + 1)
                    : ucm::usbParameterFieldNameV2(descriptor.fieldId)},
                {QStringLiteral("group"),
                    ucm::usbParameterGroupNameV2(descriptor.groupId)},
                {QStringLiteral("current"), current},
                {QStringLiteral("draft"), draft},
                {QStringLiteral("minimum"),
                    static_cast<qint64>(descriptor.minimumValue)},
                {QStringLiteral("maximum"),
                    static_cast<qint64>(descriptor.maximumValue)},
                {QStringLiteral("step"), descriptor.stepValue >= 1.0
                    ? static_cast<qint64>(descriptor.stepValue) : 1},
                {QStringLiteral("allowedValues"), allowedValues},
                {QStringLiteral("dirty"), draft != current},
                {QStringLiteral("canWrite"), runtimeConfigurationCanWrite()},
                {QStringLiteral("constraint"),
                    ucm::usbParameterConstraintTextV2(descriptor)}
            });
        }
    }
    return fields;
}

QVariantList DashboardBridge::afeMaintenanceFields() const
{
    QVariantList fields;
    for (const QVariant &field : runtimeConfigurationFields()) {
        const int fieldId = field.toMap()
            .value(QStringLiteral("fieldId")).toInt();
        if (fieldId == 16 || fieldId == 18 || fieldId == 19
            || fieldId == static_cast<int>(
                ucm::UsbParameterCaptureWindowStartV2))
            fields.push_back(field);
    }
    return fields;
}

bool DashboardBridge::afeMaintenanceHasChanges() const
{
    for (auto item = m_runtimeParameterDraft.cbegin();
         item != m_runtimeParameterDraft.cend(); ++item) {
        const int fieldId = item.key()
            .section(QLatin1Char(':'), 0, 0).toInt();
        if (fieldId == 16 || fieldId == 18 || fieldId == 19
            || fieldId == static_cast<int>(
                ucm::UsbParameterCaptureWindowStartV2))
            return true;
    }
    return false;
}

void DashboardBridge::setRuntimeConfigurationField(
    int fieldId, int elementIndex, const QVariant &value)
{
    if (!runtimeConfigurationCanWrite()) {
        m_runtimeParameterStatus = QStringLiteral(
            "本地候选未修改：需要工程师登录、HOST_MANAGED、有效租约、硬件安全停止和UHW2确认卸载。");
        emit stateChanged();
        return;
    }
    bool valueOk = false;
    const qint64 candidateValue = value.toString().trimmed().toLongLong(&valueOk);
    const auto descriptor = std::find_if(
        m_usbV2Discovery.parameters.cbegin(),
        m_usbV2Discovery.parameters.cend(),
        [fieldId](const ucm::UsbParameterDescriptorV2 &item) {
            return item.fieldId == static_cast<quint32>(fieldId);
        });
    if (!valueOk || descriptor == m_usbV2Discovery.parameters.cend()) {
        m_runtimeParameterStatus = QStringLiteral("字段%1的候选不是完整整数。")
            .arg(fieldId);
        emit stateChanged();
        return;
    }
    ucm::UsbRuntimeConfigObjectV9 candidate =
        m_runtimeConfig.receipt.activeConfiguration;
    QString error;
    const ucm::UsbRuntimeParameterEditV9 edit {
        static_cast<quint32>(fieldId), elementIndex, candidateValue};
    if (!ucm::applyUsbRuntimeParameterEditV9(
            &candidate, *descriptor,
            m_usbV2Discovery.capabilities.activeConfigGroupMask,
            edit, &error)) {
        m_runtimeParameterStatus = error;
        emit stateChanged();
        return;
    }
    qint64 activeValue = 0;
    if (!ucm::usbRuntimeParameterValueV9(
            m_runtimeConfig.receipt.activeConfiguration,
            static_cast<quint32>(fieldId), elementIndex,
            &activeValue, &error)) {
        m_runtimeParameterStatus = error;
        emit stateChanged();
        return;
    }
    const QString key = QStringLiteral("%1:%2").arg(fieldId).arg(elementIndex);
    if (candidateValue == activeValue) m_runtimeParameterDraft.remove(key);
    else m_runtimeParameterDraft.insert(key, candidateValue);
    m_runtimeParameterStatus = m_runtimeParameterDraft.isEmpty()
        ? QStringLiteral("运行参数候选与ARM活动配置一致。")
        : QStringLiteral("已编辑%1项运行参数；尚未发送。").arg(
            m_runtimeParameterDraft.size());
    emit stateChanged();
}

void DashboardBridge::resetRuntimeConfigurationDraft()
{
    if (m_runtimeParameterBusy) return;
    m_runtimeParameterDraft.clear();
    m_runtimeParameterStatus = QStringLiteral("已放弃本地运行参数候选。");
    emit stateChanged();
}

void DashboardBridge::resetAfeMaintenanceDraft()
{
    if (m_runtimeParameterBusy) return;
    for (auto item = m_runtimeParameterDraft.begin();
         item != m_runtimeParameterDraft.end();) {
        const int fieldId = item.key()
            .section(QLatin1Char(':'), 0, 0).toInt();
        if (fieldId == 16 || fieldId == 18 || fieldId == 19
            || fieldId == static_cast<int>(
                ucm::UsbParameterCaptureWindowStartV2))
            item = m_runtimeParameterDraft.erase(item);
        else
            ++item;
    }
    m_runtimeParameterStatus = QStringLiteral(
        "已放弃本地AFE维护候选。");
    emit stateChanged();
}

void DashboardBridge::applyAfeMaintenance(bool authorized)
{
    for (auto item = m_runtimeParameterDraft.cbegin();
         item != m_runtimeParameterDraft.cend(); ++item) {
        const int fieldId = item.key()
            .section(QLatin1Char(':'), 0, 0).toInt();
        if (fieldId != 16 && fieldId != 18 && fieldId != 19
            && fieldId != static_cast<int>(
                ucm::UsbParameterCaptureWindowStartV2)) {
            m_runtimeParameterStatus = QStringLiteral(
                "未发送：还存在非AFE运行参数候选，请先在设备配置页处理，避免隐式合并事务。");
            emit stateChanged();
            return;
        }
    }
    if (!afeMaintenanceHasChanges()) {
        m_runtimeParameterStatus = QStringLiteral(
            "没有待应用的AFE维护候选。");
        emit stateChanged();
        return;
    }
    applyRuntimeConfiguration(authorized);
}

void DashboardBridge::applyRuntimeConfiguration(bool authorized)
{
    if (!authorized || !runtimeConfigurationCanWrite()
        || m_runtimeParameterDraft.isEmpty()) {
        m_runtimeParameterStatus = m_runtimeParameterDraft.isEmpty()
            ? QStringLiteral("没有待应用的运行参数候选。")
            : QStringLiteral("未发送：运行参数安全写门尚未全部满足。");
        emit stateChanged();
        return;
    }
    QVariantList edits;
    for (auto item = m_runtimeParameterDraft.cbegin();
         item != m_runtimeParameterDraft.cend(); ++item) {
        const QStringList parts = item.key().split(QLatin1Char(':'));
        if (parts.size() != 2) continue;
        edits.push_back(QVariantMap {
            {QStringLiteral("fieldId"), parts[0]},
            {QStringLiteral("elementIndex"), parts[1]},
            {QStringLiteral("value"), QString::number(item.value())}
        });
    }
    m_runtimeParameterBusy = true;
    m_runtimeParameterStatus = QStringLiteral(
        "正在应用%1项运行参数并核对活动配置与硬件实际值…")
            .arg(edits.size());
    emit stateChanged();
    emit runtimeParameterOperationRequested(
        ucm::UsbRuntimeConfigApplyV9, edits);
}

void DashboardBridge::saveRuntimeConfigurationStartup(bool authorized)
{
    if (!authorized || !runtimeConfigurationCanWrite()
        || !m_runtimeParameterDraft.isEmpty()) {
        m_runtimeParameterStatus = !m_runtimeParameterDraft.isEmpty()
            ? QStringLiteral("请先应用并回读全部候选，再保存开机配置。")
            : QStringLiteral("未发送：运行参数持久化安全门尚未满足。");
        emit stateChanged();
        return;
    }
    m_runtimeParameterBusy = true;
    m_runtimeParameterStatus = QStringLiteral(
        "正在保存ARM当前活动运行配置并独立回读A/B启动槽…");
    emit stateChanged();
    emit runtimeParameterOperationRequested(
        ucm::UsbRuntimeConfigSaveStartupV9, {});
}

bool DashboardBridge::configurationHasChanges() const
{
    return !ucm::diff(m_session.active(), m_session.candidate()).isEmpty();
}

bool DashboardBridge::configurationPrepared() const
{
    return m_session.phase() == ucm::ApplyPhase::ValidationPassed;
}

bool DashboardBridge::configurationCanApply() const
{
    // The legacy complete-configuration editor remains an offline draft.
    // Product writes use the revision 9 model and input-policy operations.
    return false;
}

void DashboardBridge::setConfigurationField(const QString &key,
                                             const QVariant &value)
{
    if (!engineerAuthorized()) {
        m_configurationStatus = QStringLiteral(
            "客户模式已锁定配置编辑；请验证工程师 PIN。") ;
        emit stateChanged();
        return;
    }
    if (m_configurationBusy || m_controlModeBusy) return;
    if (m_authorityOperationInFlight || m_authorityOperationQueued) {
        m_configurationStatus = QStringLiteral(
            "正在完成短暂的控制权续租；本次编辑已排队，不会丢失");
        emit stateChanged();
        QTimer::singleShot(100, this, [this, key, value] {
            setConfigurationField(key, value);
        });
        return;
    }
    if (m_configurationRequiresReconnect) {
        m_configurationStatus = QStringLiteral(
            "上次设备提交结果未闭合；请先重连并重读活动配置。") ;
        emit stateChanged();
        return;
    }
    ucm::Configuration candidate = m_session.candidate();
    // cfg_version belongs to the prepared object, never to the editable draft.
    candidate.cfgVersion = m_session.active().cfgVersion;
    bool ok = false;
    const auto integerValue = [&value, &ok]() {
        return value.toString().trimmed().toInt(&ok);
    };
    const auto decimalValue = [&value, &ok]() {
        return value.toString().trimmed().toDouble(&ok);
    };

    if (key.startsWith(QStringLiteral("lna_gain_db_rod_"))) {
        const int rod = key.mid(QStringLiteral("lna_gain_db_rod_").size())
                            .toInt(&ok) - 1;
        const int gain = integerValue();
        if (ok && rod >= 0 && rod < 4
            && (gain == 12 || gain == 18 || gain == 24)) {
            candidate.afe.lnaGainDbByRod[rod] = gain;
        } else {
            ok = false;
        }
    } else if (key.startsWith(QStringLiteral("digital_gain_steps_rod_"))) {
        const int rod = key.mid(
            QStringLiteral("digital_gain_steps_rod_").size()).toInt(&ok) - 1;
        const int steps = integerValue();
        if (ok && rod >= 0 && rod < 4 && steps >= 0 && steps <= 30) {
            candidate.afe.digitalGainStepsByRod[rod] = steps;
        } else {
            ok = false;
        }
    } else if (key == QStringLiteral("digital_tgc_attenuation_db")) {
        candidate.afe.digitalTgcAttenuationDb = integerValue();
    } else if (key == QStringLiteral("template_confirm_frames")) {
        candidate.algorithm.templateConfirmFrames = integerValue();
    } else if (key == QStringLiteral("ncc_peak_threshold")) {
        candidate.algorithm.nccPeakThreshold = decimalValue();
    } else if (key == QStringLiteral("rod_length_mm")) {
        candidate.algorithm.rodLengthMm = integerValue();
    } else if (key == QStringLiteral("measurement_point_mm")) {
        candidate.algorithm.measurementPointMm = integerValue();
    } else if (key == QStringLiteral("minimum_valid_force_n")) {
        candidate.algorithm.minimumValidForceN = integerValue();
    } else if (key == QStringLiteral("imbalance_alarm_threshold")) {
        candidate.algorithm.imbalanceAlarmThreshold = decimalValue();
    } else if (key == QStringLiteral("consecutive_alarm_frames")) {
        candidate.algorithm.consecutiveAlarmFrames = integerValue();
    }

    if (!ok) {
        m_configurationStatus = QStringLiteral("输入未采用：%1 不是完整数值或白名单选项。")
            .arg(key);
        emit stateChanged();
        return;
    }

    m_session.setCandidate(candidate);
    m_configurationChanges = ucm::diff(m_session.active(), candidate);
    m_configurationErrors.clear();
    m_configurationIdentity.clear();
    m_configurationStatus = m_configurationChanges.isEmpty()
        ? QStringLiteral("候选值与活动配置一致。")
        : QStringLiteral("已编辑 %1 项；尚未生成或发送配置对象。")
              .arg(m_configurationChanges.size());
    emit stateChanged();
}

void DashboardBridge::resetConfigurationDraft()
{
    if (m_configurationBusy || m_controlModeBusy) return;
    if (m_authorityOperationInFlight || m_authorityOperationQueued) {
        QTimer::singleShot(100, this,
                           &DashboardBridge::resetConfigurationDraft);
        return;
    }
    if (m_configurationRequiresReconnect) {
        m_configurationStatus = QStringLiteral(
            "设备应用结果未闭合；恢复按钮不能替代真实重连回读。") ;
        emit stateChanged();
        return;
    }
    m_session.setCandidate(m_session.active());
    m_session.resetCycle();
    m_configurationChanges.clear();
    m_configurationErrors.clear();
    const ucm::ConfigIdentity identity = ucm::identityFor(m_session.active());
    m_configurationIdentity = QStringLiteral("活动 CRC %1 · SHA256 %2…")
        .arg(identity.crc32, identity.sha256.left(16));
    m_configurationStatus = m_connected
        ? QStringLiteral("已从真实USB读取ARM活动配置；当前仅编辑本地候选。")
        : QStringLiteral("真板未连接；显示产品默认对象，仅供本地编辑和校验。") ;
    emit stateChanged();
}

void DashboardBridge::prepareConfiguration()
{
    if (!engineerAuthorized()) {
        m_configurationErrors = {
            QStringLiteral("客户模式不允许准备设备配置。")};
        m_configurationStatus = QStringLiteral("配置准备已阻断。") ;
        emit stateChanged();
        return;
    }
    if (m_configurationBusy || m_controlModeBusy) return;
    if (m_authorityOperationInFlight || m_authorityOperationQueued) {
        m_configurationStatus = QStringLiteral(
            "正在完成短暂的控制权续租；校验请求已排队");
        emit stateChanged();
        QTimer::singleShot(100, this,
                           &DashboardBridge::prepareConfiguration);
        return;
    }
    if (m_configurationRequiresReconnect) {
        m_configurationErrors = {
            QStringLiteral("必须先重连，不能在未知设备状态上准备新配置。")};
        m_configurationStatus = QStringLiteral("准备已阻断。") ;
        emit stateChanged();
        return;
    }
    if (!configurationHasChanges()) {
        m_configurationErrors = {QStringLiteral("尚未修改任何可调白名单字段。")};
        m_configurationStatus = QStringLiteral("未生成待提交对象。") ;
        emit stateChanged();
        return;
    }
    if (!m_localSelfTestAccess) {
        m_configurationErrors = {QStringLiteral(
            "旧完整配置编辑器仅供离线预览；产品配置请使用revision 9型号／输入策略事务。")};
        m_configurationStatus = QStringLiteral("旧配置准备已阻断。") ;
        emit stateChanged();
        return;
    }
    if (!m_session.prepare()) {
        m_configurationErrors = {m_session.lastMessage()};
        m_configurationStatus = QStringLiteral("准备失败；未发送到设备。") ;
        emit stateChanged();
        return;
    }
    if (!m_session.validateCandidate()) {
        m_configurationErrors = m_session.lastValidation().errors;
        m_configurationChanges = ucm::diff(m_session.active(),
                                            m_session.candidate());
        m_configurationStatus = QStringLiteral("本地校验失败；未发送到设备。") ;
        emit stateChanged();
        return;
    }

    const ucm::ConfigIdentity identity = ucm::identityFor(m_session.candidate());
    m_configurationIdentity = QStringLiteral("候选 CRC %1 · SHA256 %2")
        .arg(identity.crc32, identity.sha256);
    m_configurationChanges = ucm::diff(m_session.active(),
                                        m_session.candidate());
    m_configurationErrors.clear();
    m_configurationStatus = QStringLiteral(
        "完整配置对象已准备并通过本地校验；尚未发送到USB/ARM。") ;
    emit stateChanged();
}

void DashboardBridge::applyPreparedConfiguration()
{
    if (!engineerAuthorized()) {
        m_configurationErrors = {
            QStringLiteral("客户模式不允许向设备提交配置。")};
        m_configurationStatus = QStringLiteral("配置提交已阻断。") ;
        emit stateChanged();
        return;
    }
    if (!configurationCanApply()) {
        m_configurationErrors = {QStringLiteral(
            "要求真实USB在线且候选对象已通过本地校验。")};
        m_configurationStatus = QStringLiteral("易失应用未执行。") ;
        emit stateChanged();
        return;
    }

    m_configurationBusy = true;
    m_timer.stop();
    m_configurationErrors.clear();
    m_configurationStatus = m_pollInFlight
        ? QStringLiteral("正在等待当前USB读取结束，随后提交并独立回读…")
        : QStringLiteral("正在后台向ARM提交并独立回读；界面可继续响应…");
    emit stateChanged();
    if (m_pollInFlight) {
        m_configurationApplyQueued = true;
        return;
    }
    dispatchPreparedConfiguration();
}

void DashboardBridge::dispatchPreparedConfiguration()
{
    if (!m_configurationBusy) return;
    if (!m_connected) {
        m_configurationBusy = false;
        m_configurationRequiresReconnect = true;
        m_configurationErrors = {
            QStringLiteral("提交前USB连接已失效；未启动新的提交，请重连。")};
        m_configurationStatus = QStringLiteral(
            "USB已离线；配置编辑已锁定，等待重连回读。") ;
        emit stateChanged();
        return;
    }
    emit configurationApplyRequested();
}

void DashboardBridge::finishPreparedConfiguration(
    const ConfigurationApplyResult &result)
{
    m_configurationBusy = false;
    m_configurationApplyQueued = false;
    if (!result.committed) {
        m_configurationErrors = {result.message};
        m_configurationRequiresReconnect =
            !result.failurePreservesActive;
        m_configurationStatus = result.failurePreservesActive
            ? QStringLiteral(
                "ARM未受理或已明确拒绝；活动配置保持不变，可直接修改后重试。")
            : QStringLiteral(
                "提交未闭合，设备状态可能未知；已锁定配置编辑，请重连回读。");
        if (result.failurePreservesActive && m_connected
            && !m_timer.isActive()) {
            m_timer.start();
        }
        emit stateChanged();
        if (result.failurePreservesActive) refreshNow();
        return;
    }

    if (!result.confirmed) {
        m_configurationErrors = {result.message};
        m_configurationRequiresReconnect =
            !result.failurePreservesActive;
        m_configurationStatus = result.failurePreservesActive
            ? QStringLiteral(
                "ARM已给出确定终态；新配置未提升，旧活动配置保持不变，可直接修改后重试。")
            : QStringLiteral(
                "设备回读未闭合；已锁定编辑，请重连确认活动配置。");
        if (result.failurePreservesActive && m_connected
            && !m_timer.isActive()) {
            m_timer.start();
        }
        emit stateChanged();
        if (result.failurePreservesActive) refreshNow();
        return;
    }

    m_configurationIdentity = QStringLiteral("活动 CRC %1 · SHA256 %2")
        .arg(result.activeIdentity.crc32, result.activeIdentity.sha256);
    m_configurationChanges.clear();
    m_configurationErrors.clear();
    m_configurationStatus = QStringLiteral(
        "易失应用成功，设备独立回读与候选字段/SHA一致；断电后不保留。") ;
    m_evidenceStatus = QStringLiteral(
        "配置已确认；进入证据页后可刷新并冻结最新回执。") ;
    if (m_connected && !m_timer.isActive()) m_timer.start();
    emit stateChanged();
    refreshNow();
}

void DashboardBridge::refreshEvidenceSummary()
{
    if (productReadOnly() || m_offlinePreview) {
        if (m_exportBusy) return;
        m_evidenceItems = {QVariantMap {{"label", QStringLiteral("产品revision9诊断")},
            {"value", m_connected ? QStringLiteral("可读取设备证据") : QStringLiteral("离线")},
            {"note", QStringLiteral("原始日志、产品状态、能力、CSV及SHA256；失败明确标记READS_INCOMPLETE")},
            {"ready", m_connected}}};
        m_evidenceStatus = m_connected ? QStringLiteral("可导出产品诊断包；任一设备读取失败会保存错误清单并标记采集不完整")
            : QStringLiteral("离线预览不读取USB；连接后才可导出设备诊断包");
        emit stateChanged();
        return;
    }
    if (!m_access.engineerMode()) {
        m_evidenceItems.clear();
        m_evidenceStatus = QStringLiteral(
            "客户模式不展开工程证据；历史事件仍保持只读可见。") ;
        emit stateChanged();
        return;
    }
    if (m_configurationBusy || m_authorityOperationInFlight
        || m_authorityOperationQueued) {
        m_evidenceStatus = QStringLiteral(
            "USB配置或控制权事务进行中；完成后再刷新证据。") ;
        emit stateChanged();
        return;
    }
    if (m_connected && m_logSourceRecords.isEmpty()) {
        refreshLogSources();
    }
    ucm::TransportInfo transport;
    ucm::ConfigurationReceiptDetails receipt;
    ucm::Configuration activeConfiguration;
    ucm::UsbExtendedDiscoveryV2 extended;
    {
        QMutexLocker lock(&m_sessionMutex);
        transport = m_session.transportInfo();
        receipt = m_session.configurationReceiptDetails();
        activeConfiguration = m_session.active();
        extended = m_session.usbExtendedDiscovery();
    }
    const ucm::ConfigIdentity activeIdentity =
        ucm::identityFor(activeConfiguration);
    const int availableLogs = std::count_if(
        m_logSourceRecords.cbegin(), m_logSourceRecords.cend(),
        [](const ucm::DeviceLogSource &source) { return source.available; });
    const bool r3SourceAvailable = std::any_of(
        m_logSourceRecords.cbegin(), m_logSourceRecords.cend(),
        [](const ucm::DeviceLogSource &source) {
            return source.sourceId == 4U && source.available;
        });
    const bool timelineParsed = m_timelineStatus.startsWith(
        QStringLiteral("已解析"));
    const bool waveformReady = m_waveformSeries.size() == 4
        && m_waveformSampleRateHz != 0U;

    const auto item = [](const QString &title, const QString &state,
                         const QString &detail, const QString &accent,
                         bool ready) {
        return QVariantMap {
            {QStringLiteral("title"), title},
            {QStringLiteral("state"), state},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("accent"), accent},
            {QStringLiteral("ready"), ready}
        };
    };
    m_evidenceItems = {
        item(QStringLiteral("USB / ARM receiver"),
             m_connected ? QStringLiteral("在线") : QStringLiteral("缺失"),
             QStringLiteral("%1 · scope=%2")
                 .arg(transport.name, transport.scope),
             m_connected ? QStringLiteral("#2AA198") : QStringLiteral("#D84F4F"),
             m_connected),
        item(QStringLiteral("USB V2能力目录"),
             extended.catalogReady
                 ? QStringLiteral("%1 项可冻结")
                       .arg(extended.parameters.size())
                 : (extended.available ? QStringLiteral("能力已识别，目录异常")
                                       : QStringLiteral("仅V1兼容")),
             extended.message,
             extended.catalogReady ? QStringLiteral("#2AA198")
                                   : QStringLiteral("#F0A84B"),
             extended.catalogReady),
        item(QStringLiteral("ARM控制权状态"),
             m_controlAuthority.available
                 ? QStringLiteral("128 B 可冻结")
                 : QStringLiteral("状态不可用"),
             m_controlAuthority.available
                 ? QStringLiteral("%1 · %2 · hardware=%3")
                       .arg(deviceControlModeText(),
                            deviceControlPhaseText(),
                            m_controlAuthority.hardwareActive
                                ? QStringLiteral("active")
                                : QStringLiteral("safe-off"))
                 : m_controlAuthorityStatus,
             m_controlAuthority.available ? QStringLiteral("#4D7CFE")
                                          : QStringLiteral("#D84F4F"),
             m_controlAuthority.available),
        item(QStringLiteral("活动配置与回执"),
             receipt.available ? QStringLiteral("512 B 可冻结")
                               : QStringLiteral("回执不可用"),
             QStringLiteral("cfg %1 · CRC %2 · persisted=false")
                  .arg(activeConfiguration.cfgVersion, activeIdentity.crc32),
             receipt.available ? QStringLiteral("#4D7CFE") : QStringLiteral("#D84F4F"),
             receipt.available),
        item(QStringLiteral("正式遥测对象"),
             m_telemetryReady ? QStringLiteral("512 B 就绪")
                              : QStringLiteral("未就绪"),
             m_telemetryReady
                 ? QStringLiteral("真实四杆快照 · %1").arg(m_processStateText)
                 : m_statusText,
             m_telemetryReady ? QStringLiteral("#2AA198") : QStringLiteral("#F0A84B"),
             m_telemetryReady),
        item(QStringLiteral("标准力相机"),
             m_referenceForce.ready() ? QStringLiteral("四杆标准力就绪")
                                      : QStringLiteral("未捕获"),
             m_referenceForce.statusText(),
             m_referenceForce.ready() ? QStringLiteral("#2AA198")
                                      : QStringLiteral("#F0A84B"),
             m_referenceForce.ready()),
        item(QStringLiteral("2048 × 4 波形"),
             waveformReady ? QStringLiteral("16448 B 就绪")
                           : QStringLiteral("未就绪"),
             waveformReady
                 ? QStringLiteral("%1 Hz · latest-only").arg(m_waveformSampleRateHz)
                 : m_waveformStatus,
             waveformReady ? QStringLiteral("#8B6FCB") : QStringLiteral("#F0A84B"),
             waveformReady),
        item(QStringLiteral("固定外设日志"),
             availableLogs == 4 ? QStringLiteral("4 / 4 可用")
                                : QStringLiteral("%1 / 4 已缓存").arg(availableLogs),
             m_logStatus,
             availableLogs == 4 ? QStringLiteral("#2AA198") : QStringLiteral("#F0A84B"),
             availableLogs == 4),
        item(QStringLiteral("R3 事件源"),
             timelineParsed
                 ? QStringLiteral("%1 条已解析").arg(m_timelineEvents.size())
                 : (r3SourceAvailable ? QStringLiteral("source 4 可冻结")
                                      : QStringLiteral("source 4 缺失")),
             timelineParsed ? m_timelineStatus
                            : QStringLiteral("诊断包先保存原始日志；时间线页负责13列契约解析。"),
             r3SourceAvailable ? QStringLiteral("#4D7CFE") : QStringLiteral("#F0A84B"),
             r3SourceAvailable)
    };
    m_evidenceStatus = m_connected
        ? QStringLiteral("真实USB在线；导出时将重新冻结所有必需对象，任一失败则不生成完成包。")
        : QStringLiteral("完整诊断包要求真实WinUSB和ARM receiver在线。") ;
    emit stateChanged();
}

void DashboardBridge::exportDiagnosticPackage(const QUrl &destination)
{
    if (productReadOnly()) {
        if (!canExportEvidence() || !destination.isLocalFile() || m_offlinePreview) return;
        m_exportBusy = true;
        m_evidenceStatus = QStringLiteral("正在后台导出产品状态、能力与原始日志；每轮只读一个分块…");
        emit productPackageRequested(destination.toLocalFile(), armForceCsv());
        emit stateChanged();
        return;
    }
    if (!m_access.engineerMode()) {
        m_evidenceStatus = QStringLiteral(
            "客户模式不允许导出工程诊断包。") ;
        emit stateChanged();
        return;
    }
    if (m_configurationBusy || m_authorityOperationInFlight
        || m_authorityOperationQueued) {
        m_evidenceStatus = QStringLiteral(
            "USB配置或控制权事务进行中；完成前不冻结可能不一致的证据包。") ;
        emit stateChanged();
        return;
    }
    const auto fail = [this](const QString &message) {
        m_evidenceStatus = QStringLiteral("诊断包未生成：%1").arg(message);
        m_evidencePath.clear();
        emit stateChanged();
    };
    if (!m_connected) {
        fail(QStringLiteral("真实WinUSB或ARM receiver未就绪。"));
        return;
    }
    if (!destination.isLocalFile()) {
        fail(QStringLiteral("请选择本机文件夹。"));
        return;
    }
    const QString basePath = QDir::cleanPath(destination.toLocalFile());
    const QDir baseDirectory(basePath);
    if (!baseDirectory.exists() || !QDir::isAbsolutePath(basePath)) {
        fail(QStringLiteral("目标文件夹不存在或不是绝对路径。"));
        return;
    }

    m_evidenceStatus = QStringLiteral("正在冻结真实设备对象，请勿断开USB…");
    emit stateChanged();
    QMutexLocker sessionLock(&m_sessionMutex);
    QString leaseError;
    const auto maintainHostLease = [this, &leaseError] {
        if (!m_controlAuthority.available
            || m_controlAuthority.appliedMode
                != ucm::kUsbControlModeHostManagedV2
            || m_controlAuthority.phase
                != ucm::kUsbControlPhaseHostActiveV2) {
            return true;
        }
        const ucm::ControlAuthorityResultV2 renewed =
            m_session.renewControlLease();
        if (!renewed.success) {
            leaseError = renewed.message;
            return false;
        }
        applyControlAuthorityState(renewed.state);
        return true;
    };
    if (!maintainHostLease()) {
        fail(QStringLiteral("诊断冻结前续租失败：%1").arg(leaseError));
        return;
    }
    const ucm::BinaryObjectResult receipt = m_session.readConfigReceiptObject();
    const ucm::BinaryObjectResult telemetry = m_session.readTelemetryObject();
    const ucm::BinaryObjectResult waveform = m_session.readWaveformObject();
    const ucm::BinaryObjectResult authority =
        m_session.readControlAuthorityObject();
    const ucm::UsbCompositeRuntimeStatusResultV1 composite =
        m_session.readCompositeRuntimeStatus();
    const ucm::UsbExtendedDiscoveryV2 extended =
        m_session.usbExtendedDiscovery();
    const ucm::BinaryObjectResult extendedCapabilities = extended.available
        ? m_session.readUsbExtendedCapabilitiesObject()
        : ucm::BinaryObjectResult {};
    const ucm::BinaryObjectResult parameterCatalog = extended.available
        ? m_session.readUsbParameterCatalogObject()
        : ucm::BinaryObjectResult {};
    if (!maintainHostLease()) {
        fail(QStringLiteral("诊断对象冻结期间续租失败：%1")
                 .arg(leaseError));
        return;
    }
    const ucm::DeviceLogListResult logList = m_session.readLogSources();
    const QJsonObject sessionEvidence = m_session.evidence();
    const ucm::TransportInfo transport = m_session.transportInfo();
    if (!receipt.success || receipt.data.size() != 512
        || !telemetry.success || telemetry.data.size() != 512
        || !waveform.success || waveform.data.size() != 16448
        || !authority.success || authority.data.size() != 128
        || !logList.success
        || (extended.available
            && (!extendedCapabilities.success || !parameterCatalog.success))) {
        fail(QStringLiteral("冻结失败：receipt=%1；telemetry=%2；waveform=%3；authority=%4；logs=%5；USB V2=%6/%7")
                 .arg(receipt.message, telemetry.message,
                      waveform.message, authority.message,
                      logList.message,
                      extendedCapabilities.message,
                      parameterCatalog.message));
        return;
    }

    QVector<ucm::DiagnosticPackageFile> files {
        {QStringLiteral("config-evidence.json"),
         QJsonDocument(sessionEvidence).toJson(QJsonDocument::Indented)},
        {QStringLiteral("config-receipt-512.bin"), receipt.data},
        {QStringLiteral("telemetry-512.bin"), telemetry.data},
        {QStringLiteral("waveform-16448.bin"), waveform.data},
        {QStringLiteral("usb-authority-state-v2-128.bin"), authority.data}
    };
    const ucm::CompoundRuntimePresentation compositePresentation =
        ucm::presentCompoundRuntimeStatus(composite.snapshot);
    QJsonObject compositeDiagnostic = ucm::compoundRuntimeDiagnosticJson(
        composite.snapshot, compositePresentation);
    const bool compositeRawIncluded =
        composite.rawObject.size() == ucm::kUsbCompositeRuntimeBytesV1;
    compositeDiagnostic.insert(QStringLiteral("decode_success"),
                               composite.success);
    compositeDiagnostic.insert(
        QStringLiteral("decode_code"),
        ucm::usbCompositeRuntimeDecodeCodeTextV1(composite.code));
    compositeDiagnostic.insert(QStringLiteral("decode_message"),
                               composite.message);
    compositeDiagnostic.insert(QStringLiteral("trust_source"),
                               composite.trustEvidence);
    compositeDiagnostic.insert(QStringLiteral("raw_wire_bytes_included"),
                               compositeRawIncluded);
    files.push_back({QStringLiteral("compound-runtime-status.json"),
                     QJsonDocument(compositeDiagnostic)
                         .toJson(QJsonDocument::Indented)});
    if (compositeRawIncluded) {
        files.push_back({
            QStringLiteral("usb-composite-runtime-status-v1-384.bin"),
            composite.rawObject});
    }
    if (extended.available) {
        files.push_back({QStringLiteral("usb-v2-capabilities-256.bin"),
                         extendedCapabilities.data});
        files.push_back({QStringLiteral("usb-v2-parameter-catalog.bin"),
                         parameterCatalog.data});
    }
    bool referenceForceCaptured = false;
    if (m_referenceForce.ready()) {
        const ReferenceForceFrame reference = m_referenceForce.latest();
        QJsonArray forces;
        for (double force : reference.forceKn) forces.append(force);
        const QJsonObject referencePayload {
            {QStringLiteral("schema_version"), QStringLiteral("forceInput_v1")},
            {QStringLiteral("timestamp_ms"),
             static_cast<double>(reference.timestampMs)},
            {QStringLiteral("force_kN"), forces},
            {QStringLiteral("source"), QStringLiteral("force_input_camera")},
            {QStringLiteral("evidence_level"),
             QStringLiteral("force_input_camera")},
            {QStringLiteral("confidence"), reference.confidence},
            {QStringLiteral("status"), QStringLiteral("ok")}
        };
        files.push_back({QStringLiteral("reference-force.json"),
                         QJsonDocument(referencePayload)
                             .toJson(QJsonDocument::Indented)});
        referenceForceCaptured = true;
    }
    QJsonArray logMetadata;
    for (const ucm::DeviceLogSource &source : logList.sources) {
        QJsonObject metadata {
            {QStringLiteral("source_id"), static_cast<int>(source.sourceId)},
            {QStringLiteral("name"), source.name},
            {QStringLiteral("available"), source.available},
            {QStringLiteral("total_bytes"), QString::number(source.totalBytes)},
            {QStringLiteral("modified_time_ns"), QString::number(source.modifiedTimeNs)},
            {QStringLiteral("snapshot_id"), QString::number(source.snapshotId)},
            {QStringLiteral("file_identity_crc32"),
             hexValue(source.fileIdentityCrc32, 8)}
        };
        if (source.available) {
            if (!maintainHostLease()) {
                fail(QStringLiteral("日志冻结期间续租失败：%1")
                         .arg(leaseError));
                return;
            }
            constexpr quint64 maximum = 65536U;
            const quint64 offset = source.totalBytes > maximum
                ? source.totalBytes - maximum : 0U;
            QByteArray data;
            if (source.totalBytes != 0U) {
                const ucm::DeviceLogChunkResult chunk = m_session.readLogChunk(
                    source.sourceId, offset,
                    static_cast<quint32>(source.totalBytes - offset),
                    source.snapshotId);
                if (!chunk.success) {
                    fail(QStringLiteral("日志%1冻结失败：%2")
                             .arg(source.sourceId).arg(chunk.message));
                    return;
                }
                data = chunk.data;
            }
            const QString fileName = QStringLiteral("log-%1-tail.txt")
                .arg(source.sourceId);
            files.push_back({fileName, data});
            metadata.insert(QStringLiteral("captured_file"), fileName);
            metadata.insert(QStringLiteral("captured_offset"),
                            QString::number(offset));
            metadata.insert(QStringLiteral("captured_bytes"), data.size());
        }
        logMetadata.append(metadata);
    }
    sessionLock.unlock();

    QQuickWindow *window = nullptr;
    for (QWindow *candidate : QGuiApplication::allWindows()) {
        window = qobject_cast<QQuickWindow *>(candidate);
        if (window != nullptr && window->isVisible()) break;
        window = nullptr;
    }
    QByteArray screenshot;
    QBuffer screenshotBuffer(&screenshot);
    if (window == nullptr || !screenshotBuffer.open(QIODevice::WriteOnly)
        || !window->grabWindow().save(&screenshotBuffer, "PNG")) {
        fail(QStringLiteral("当前界面截图采集失败。"));
        return;
    }
    files.push_back({QStringLiteral("screen.png"), screenshot});

    const QJsonObject manifest {
        {QStringLiteral("schema"), QStringLiteral("UCM.DIAGNOSTIC.PACKAGE.1")},
        {QStringLiteral("generated_at_utc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("application"), QStringLiteral("UcmConfigStudioNext")},
        {QStringLiteral("transport"), transport.name},
        {QStringLiteral("configuration_persisted"), false},
        {QStringLiteral("reference_force_captured"),
         referenceForceCaptured},
        {QStringLiteral("reference_force_file"),
         referenceForceCaptured ? QStringLiteral("reference-force.json")
                                : QString()},
        {QStringLiteral("telemetry_bytes"), telemetry.data.size()},
        {QStringLiteral("waveform_bytes"), waveform.data.size()},
        {QStringLiteral("config_receipt_bytes"), receipt.data.size()},
        {QStringLiteral("usb_authority_state_bytes"), authority.data.size()},
        {QStringLiteral("compound_runtime_decode_success"),
         composite.success},
        {QStringLiteral("compound_runtime_decode_code"),
         ucm::usbCompositeRuntimeDecodeCodeTextV1(composite.code)},
        {QStringLiteral("compound_runtime_bytes"),
         composite.rawObject.size()},
        {QStringLiteral("runtime_chain_ready"),
         compositePresentation.runtimeChainReady},
        {QStringLiteral("compound_runtime_trust_ready"),
         composite.trustEvidence.value(QStringLiteral("ready")).toBool()},
        {QStringLiteral("formal_measurement_credit"), false},
        {QStringLiteral("usb_v2_available"), extended.available},
        {QStringLiteral("usb_v2_catalog_ready"), extended.catalogReady},
        {QStringLiteral("usb_v2_capabilities_bytes"),
         extendedCapabilities.data.size()},
        {QStringLiteral("usb_v2_parameter_catalog_bytes"),
         parameterCatalog.data.size()},
        {QStringLiteral("logs"), logMetadata}
    };
    const QString packageName = QStringLiteral("ucm-diagnostic-%1")
        .arg(QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const ucm::DiagnosticPackageResult result = ucm::writeDiagnosticPackage(
        basePath, packageName, manifest, files);
    if (!result.success) {
        fail(QStringLiteral("%1；路径=%2")
                 .arg(result.message, result.packagePath));
        return;
    }
    m_evidencePath = result.packagePath;
    m_evidenceStatus = QStringLiteral(
        "完整诊断包已生成；manifest和SHA256SUMS已闭合，configuration_persisted=false。") ;
    emit stateChanged();
}

bool DashboardBridge::configureEngineerPin(
    const QString &pin, const QString &confirmation)
{
    return m_access.configurePin(pin, confirmation);
}

bool DashboardBridge::enterEngineerMode(const QString &pin)
{
    return m_access.enterEngineerMode(pin);
}

void DashboardBridge::leaveEngineerMode()
{
    if (m_controlAuthority.available
        && m_controlAuthority.appliedMode
            == ucm::kUsbControlModeHostManagedV2) {
        m_controlAuthorityStatus = QStringLiteral(
            "设备仍由USB手动控制：请先交还ARM自主模式，再退出工程师模式");
        emit stateChanged();
        return;
    }
    if (m_referenceForce.running()) m_referenceForce.stop();
    m_access.leaveEngineerMode();
}

QVariantMap DashboardBridge::productState() const
{
    return {{QStringLiteral("supported"), m_product.supported},
            {QStringLiteral("success"), m_connected && m_product.success},
            {QStringLiteral("message"), m_connected ? m_product.message : QStringLiteral("USB离线；当前与开机配置未知")},
            {QStringLiteral("deviceModel"), m_product.deviceModel.toVariantMap()},
            {QStringLiteral("inputPolicy"), m_product.inputPolicy.toVariantMap()},
            {QStringLiteral("pairedInputStatus"), m_product.pairedInputStatus.toVariantMap()}};
}

void DashboardBridge::refreshCalibrationReadback()
{
    if (!m_connected || m_offlinePreview || m_productOperationBusy
        || m_calibrationReadback.value(QStringLiteral("busy")).toBool()) {
        if (!m_connected) {
            m_calibrationReadback = {{"busy", false}, {"outcome", "failed"},
                {"message", QStringLiteral("USB未连接，不能回读ARM参数")}};
            emit stateChanged();
        }
        return;
    }
    m_calibrationReadback = {{"busy", true}, {"outcome", "pending"},
        {"message", QStringLiteral("正在通过USB回读ARM参数")}};
    emit calibrationReadbackRequested();
    emit stateChanged();
}

void DashboardBridge::exportCsv(const QUrl &destination)
{
    if (!destination.isLocalFile()) return;
    QSaveFile file(destination.toLocalFile());
    if (!file.open(QIODevice::WriteOnly)) {
        m_statusText = file.errorString();
        emit telemetryChanged();
        return;
    }
    const QByteArray csv = armForceCsv();
    const bool written = file.write(csv) == csv.size();
    const bool saved = written && file.commit();
    m_statusText = saved ? QStringLiteral("已导出窗口内ARM杆力；无效及断连保留为空值") : file.errorString();
    emit telemetryChanged();
}

void DashboardBridge::scheduleReconnect()
{
    if (m_offlinePreview || m_manuallyDisconnected || m_reconnectScheduled || m_connected) return;
    const int delayMs = ucm::ui::connectionRetryDelayMs(m_reconnectAttempt);
    ++m_reconnectAttempt;
    m_reconnectScheduled = true;
    QTimer::singleShot(delayMs, this, [this] {
        m_reconnectScheduled = false;
        if (!m_manuallyDisconnected && !m_connected) reconnect();
    });
}

void DashboardBridge::disconnectDevice()
{
    if (m_configurationBusy || m_authorityOperationInFlight) return;
    m_calibrationValidatedSession.clear();
    m_calibrationAppliedSession.clear();
    if (m_calibrationPendingOperation != 0) {
        m_calibrationOperation = {{QStringLiteral("operation"), m_calibrationPendingOperation},
            {QStringLiteral("outcome"), QStringLiteral("unresolved")},
            {QStringLiteral("message"), QStringLiteral("已断开，标定写入结果待对账")},
            {QStringLiteral("busy"), false}};
        m_calibrationPendingOperation = 0;
        m_calibrationPendingSession.clear();
    }
    m_productCommissioned = false;
    if (!m_productOperation.isEmpty()) {
        const bool unresolved = m_productOperationBusy
            || m_productOperation.value(QStringLiteral("outcome")).toString() == QStringLiteral("pending")
            || m_productOperation.value(QStringLiteral("outcome")).toString() == QStringLiteral("unresolved");
        m_productOperation[QStringLiteral("previousEvidence")] = m_productOperation.value(QStringLiteral("fields"));
        m_productOperation[QStringLiteral("fields")] = QVariantMap {};
        m_productOperation[QStringLiteral("busy")] = false;
        if (unresolved) {
            m_productOperation[QStringLiteral("outcome")] = QStringLiteral("unresolved");
            m_productOperation[QStringLiteral("message")] = QStringLiteral("已手动断开，事务尚未对账；禁止新写入与激活");
        }
        m_productOperationBusy = false;
    }
    m_manuallyDisconnected = true;
    ++m_logToken;
    m_logBusy = false;
    m_timer.stop();
    m_authorityTimer.stop();
    emit disconnectRequested();
    m_connected = false;
    m_usbLabel = QStringLiteral("USB 已手动断开");
    m_runtimeReady = false;
    m_runtimeStateText = QStringLiteral("离线");
    m_runtimeDetailText.clear();
    clearMeasurements(QStringLiteral("USB离线"));
    clearWaveform(QStringLiteral("USB离线"));
    m_logSources.clear();
    m_logSourceRecords.clear();
    m_logContent.clear();
    m_timelineEvents.clear();
    m_controlAuthority = {};
    emit connectionChanged();
    emit waveformChanged();
    emit diagnosticsChanged();
    m_product = {};
    m_usbV2Discovery = {};
    m_trendBuffer.append(QDateTime::currentMSecsSinceEpoch(), 0, {});
    recordCsvGap();
    m_statusText = QStringLiteral("已手动断开；自动重连已暂停");
    emit stateChanged();
    emit telemetryChanged();
}

QByteArray DashboardBridge::armForceCsv() const
{
    QByteArray csv = ArmForceCsv::header();
    for (const auto &row : m_csvRows) csv += row;
    return csv;
}

void DashboardBridge::recordCsvGap(const QString &reason)
{
    if (m_csvGap) return;
    m_csvGap = true;
    m_csvRows.push_back(ArmForceCsv::gap(reason, QDateTime::currentMSecsSinceEpoch()));
    if (m_csvRows.size() > 6000) m_csvRows.pop_front();
}

void DashboardBridge::recordArmSnapshot(const ucm::TelemetrySnapshot &s)
{
    const QString identity = QStringLiteral("%1/%2/%3/%4/%5/%6")
        .arg(s.generation).arg(s.publishedMonotonicNs).arg(s.sessionId)
        .arg(s.sequence).arg(s.frameCounter).arg(s.captureRequestId);
    if (identity == m_lastCsvIdentity) return;
    m_lastCsvIdentity = identity;
    m_csvGap = false;
    m_csvRows.push_back(ArmForceCsv::measurement(s, QDateTime::currentMSecsSinceEpoch()));
    if (m_csvRows.size() > 6000) m_csvRows.pop_front();
}

void DashboardBridge::submitProductDocument(int domain, int operation, const QString &json, bool authorized)
{
    if (!engineerAuthorized() || !productCanWrite() || !authorized) {
        m_productOperation = {{"message", QStringLiteral("工程配置权限、设备能力或控制权未满足；未发送")}, {"busy", false}};
        emit stateChanged(); return;
    }
    if (domain == 0) {
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()
            || !hasCalibrationCouplingBias(parsed.object())) {
            m_productOperation = {{QStringLiteral("ok"), false},
                {QStringLiteral("outcome"), QStringLiteral("failed")},
                {QStringLiteral("message"), QStringLiteral("型号配置须包含四路合法耦合偏置，未发送")},
                {QStringLiteral("busy"), false}};
            emit stateChanged(); return;
        }
    }
    m_productOperationBusy = true;
    emit productDocumentRequested(domain, operation, json.toUtf8(), authorized, QString());
    emit stateChanged();
}

void DashboardBridge::submitCalibrationCandidate(int operation, bool authorized)
{
    const auto reject = [this](const QString &message) {
        m_productOperation = {{QStringLiteral("ok"), false},
            {QStringLiteral("outcome"), QStringLiteral("failed")},
            {QStringLiteral("message"), message}, {QStringLiteral("busy"), false}};
        m_calibrationOperation = m_productOperation;
        m_calibrationOperation.insert(QStringLiteral("operation"), 0);
        emit stateChanged();
    };
    if ((operation != 1 && operation != 2 && operation != 3)
        || !engineerAuthorized() || !productCanWrite() || !authorized || !m_calibration.candidateEvidenceIntact()) {
        reject(QStringLiteral("工程配置权限、设备能力或控制权未满足；未发送标定参数")); return;
    }
    const auto capability = productCapabilities();
    const auto capabilityKey = operation == 1 ? QStringLiteral("modelApply")
        : operation == 2 ? QStringLiteral("modelSave") : QStringLiteral("modelValidate");
    if (!capability.value(capabilityKey).toBool()) {
        reject(QStringLiteral("ARM未开放此型号配置操作；未发送")); return;
    }
    const QJsonObject candidate = QJsonObject::fromVariantMap(m_calibration.candidate());
    const QJsonObject device = m_product.deviceModel;
    const QJsonObject active = device.value(QStringLiteral("active_document")).toObject();
    const QJsonObject identity = device.value(QStringLiteral("active_identity")).toObject();
    const QJsonObject fitIdentity = candidate.value(QStringLiteral("active_model_identity")).toObject();
    const double kmat = candidate.value(QStringLiteral("kmat_unified")).toDouble();
    const QJsonArray offsets = candidate.value(QStringLiteral("coupling_bias_ns")).toArray();
    QJsonObject correctionFields;
    bool valuesValid = candidate.value(QStringLiteral("status")).toString() == QStringLiteral("candidate")
        && forceCorrectionFields(candidate, &correctionFields)
        && candidate.value(QStringLiteral("session_id")).toString() == QFileInfo(m_calibration.sessionPath()).fileName()
        && !candidate.value(QStringLiteral("formal_qualification")).toBool(true)
        && std::isfinite(kmat) && kmat > 0 && offsets.size() == 4;
    for (const auto &offset : offsets)
        valuesValid = valuesValid && offset.isDouble() && std::isfinite(offset.toDouble());
    valuesValid = valuesValid && offsets == m_calibration.sourceModel()
        .value(QStringLiteral("coupling_bias_ns")).toArray();
    const bool bodyReference =
        (active.value(QStringLiteral("schema_version")).toInt() == 2 ||
         active.value(QStringLiteral("schema_version")).toInt() == 3) &&
        active.value(QStringLiteral("geometry_model")).toString() == QStringLiteral("BODY_REFERENCE_V1");
    const bool gwEngineering = active.value(QStringLiteral("schema_version")).toInt() == 3 &&
        active.value(QStringLiteral("device_model_id")).toInteger() == 37 &&
        active.value(QStringLiteral("model_name")).toString() == QStringLiteral("GW1850R") &&
        active.value(QStringLiteral("geometry_model")).toString() ==
            QStringLiteral("GW_DRAWING_FE_ENGINEERING_V1");
    if (!valuesValid || (!bodyReference && !gwEngineering)
        || fitIdentity.isEmpty() || identity.isEmpty()) {
        reject(QStringLiteral("标定候选、会话证据或ARM型号文档无效；未发送")); return;
    }
    const QString session = candidate.value(QStringLiteral("session_id")).toString();
    if (operation == 1 && m_calibrationValidatedSession != session) {
        reject(QStringLiteral("本次候选尚未通过ARM校验；禁止应用")); return;
    }
    if (operation == 2 && m_calibrationAppliedSession != session) {
        reject(QStringLiteral("本次候选尚未收到ARM应用成功回执；禁止保存")); return;
    }
    const QString currentId = identity.value(QStringLiteral("identitySha256")).toString();
    if (currentId.size() != 64) {
        reject(QStringLiteral("ARM活动配置身份缺失；未发送")); return;
    }
    QJsonObject document = active;
    if (operation == 2) {
        QJsonObject expected = m_calibration.sourceModel();
        expected.insert(QStringLiteral("schema_version"), 3);
        expected.insert(QStringLiteral("kmat_unified"), kmat);
        expected.insert(QStringLiteral("coupling_bias_ns"), offsets);
        for (auto field = correctionFields.constBegin(); field != correctionFields.constEnd(); ++field)
            expected.insert(field.key(), field.value());
        for (auto *copy : {&expected, &document}) {
            copy->remove(QStringLiteral("configuration_generation"));
            copy->remove(QStringLiteral("device_model_config_id_sha256"));
            copy->remove(QStringLiteral("system_package_id_sha256"));
        }
        if (active.value(QStringLiteral("kmat_unified")).toDouble() != kmat
            || active.value(QStringLiteral("coupling_bias_ns")).toArray() != offsets
            || active.value(QStringLiteral("force_correction_knot_count")) !=
                correctionFields.value(QStringLiteral("force_correction_knot_count"))
            || active.value(QStringLiteral("force_correction_input_n")) !=
                correctionFields.value(QStringLiteral("force_correction_input_n"))
            || active.value(QStringLiteral("force_correction_output_n")) !=
                correctionFields.value(QStringLiteral("force_correction_output_n"))
            || identity == fitIdentity || document != expected) {
            reject(QStringLiteral("ARM当前运行参数尚未回读为本次候选；禁止保存开机配置")); return;
        }
    } else {
        if (identity != fitIdentity) {
            reject(QStringLiteral("标定以来ARM活动配置身份已改变；请重新标定")); return;
        }
        document.insert(QStringLiteral("schema_version"), 3);
        document.insert(QStringLiteral("kmat_unified"), kmat);
        document.insert(QStringLiteral("coupling_bias_ns"), offsets);
        for (auto field = correctionFields.constBegin(); field != correctionFields.constEnd(); ++field)
            document.insert(field.key(), field.value());
    }
    m_productOperationBusy = true;
    m_calibrationPendingOperation = operation;
    m_calibrationPendingSession = session;
    m_productOperation = {{QStringLiteral("ok"), true},
        {QStringLiteral("outcome"), QStringLiteral("pending")},
        {QStringLiteral("message"), QStringLiteral("正在提交标定参数操作")},
        {QStringLiteral("busy"), true}};
    m_calibrationOperation = m_productOperation;
    m_calibrationOperation.insert(QStringLiteral("operation"), operation);
    emit productDocumentRequested(0, operation,
        operation == 2 ? QByteArray() : QJsonDocument(document).toJson(QJsonDocument::Compact),
        true, QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));
    emit stateChanged();
}

void DashboardBridge::submitCustomerProduct(int operation, const QVariantMap &draft, bool authorized)
{
    if (!productCanWrite() || !authorized || (operation != 1 && operation != 2)) return;
    QJsonObject document = m_product.deviceModel.value(QStringLiteral("active_document")).toObject();
    if ((document["schema_version"].toInt() != 2 &&
         document["schema_version"].toInt() != 3) ||
        document["geometry_model"].toString() != "BODY_REFERENCE_V1") return;
    if (operation == 1) {
        QJsonObject projected;
        if (!applyCustomerDraft(document, draft, &projected)) return;
        document = projected;
    }
    if (!hasCalibrationCouplingBias(document)) {
        m_productOperation = {{QStringLiteral("ok"), false},
            {QStringLiteral("outcome"), QStringLiteral("failed")},
            {QStringLiteral("message"), QStringLiteral("当前型号配置缺少四路合法耦合偏置，禁止写入")},
            {QStringLiteral("busy"), false}};
        emit stateChanged(); return;
    }
    m_productOperationBusy = true;
    emit productDocumentRequested(0, operation, QJsonDocument(document).toJson(QJsonDocument::Compact), authorized, QString());
    emit stateChanged();
}

void DashboardBridge::stageProductUpgrade(const QUrl &file, int version, const QString &model, const QString &buildId, bool authorized)
{
    if (!engineerAuthorized() || !productCanWrite() || !file.isLocalFile() || !authorized || version <= 0) return;
    m_productOperationBusy = true;
    emit productUpgradeRequested(file.toLocalFile(), quint32(version), model, buildId, authorized);
    emit stateChanged();
}

void DashboardBridge::activateProductUpgrade(bool authorized)
{
    if (!engineerAuthorized() || !productCanWrite() || !authorized) return;
    m_productOperationBusy = true;
    emit productActivationRequested(authorized);
    emit stateChanged();
}

void DashboardBridge::queryProductOperation()
{
    if (!m_connected || m_offlinePreview || m_productOperationBusy) return;
    emit productQueryRequested();
}

void DashboardBridge::abortProductUpgrade(bool authorized)
{
    if (!engineerAuthorized() || !m_connected || !m_productCommissioned || !authorized || m_offlinePreview) return;
    emit productAbortRequested(authorized);
}

bool DashboardBridge::productCanWrite() const
{
    const auto &a = m_controlAuthority;
    return m_connected && m_productCommissioned && ucm::usbHostManagedStoppedV2(a)
        && ucm::usbRuntimeHardwareSafeForWriteV9(
            m_runtimeConfig.receipt.hardwareFlags)
        && !m_productOperationBusy && !m_exportBusy
        && !m_configurationBusy && !m_authorityOperationInFlight && !m_authorityOperationQueued && !m_controlModeBusy && !m_offlinePreview
        && m_productOperation.value(QStringLiteral("outcome")).toString() != QStringLiteral("unresolved");
}

QVariantMap DashboardBridge::productCapabilities() const
{
    const auto &c = m_usbV2Discovery.capabilities;
    const auto active = [&](quint64 feature) {
        return productCanWrite() && c.protocolRevision == 9
            && (c.supportedFeatureMask & feature) == feature && (c.activeFeatureMask & feature) == feature;
    };
    const bool model = active(ucm::kUsbExtendedFeatureDeviceModelControlV2);
    const bool policy = active(ucm::kUsbExtendedFeatureSystemInputPolicyControlV1);
    const QString reason = m_offlinePreview ? QStringLiteral("离线预览不可写")
        : !m_connected ? QStringLiteral("设备未连接")
        : !m_productCommissioned ? QStringLiteral("ARM尚未开放对应产品写能力")
        : !m_controlAuthority.available ? QStringLiteral("尚未取得有效控制权状态")
        : !productCanWrite() ? QStringLiteral("需ARM确认主机托管、硬件已停且无未闭合事务") : QString();
    return {{"commissioned", m_productCommissioned},
        {"blockReason", reason},
        {"upgradeBlockReason", reason},
        {"modelApply", model && bool(c.deviceModelOperationMask & 1)},
        {"modelSave", model && bool(c.deviceModelOperationMask & 2)},
        {"modelValidate", model && bool(c.deviceModelOperationMask & 4)},
        {"policyApply", policy && bool(c.systemInputPolicyOperationMask & 1)},
        {"policySave", policy && bool(c.systemInputPolicyOperationMask & 2)},
        {"policyValidate", policy && bool(c.systemInputPolicyOperationMask & 4)},
        {"upgradeStage", active(ucm::kUsbExtendedFeatureUpgradeStagingV2) && c.activeUpgradeTargetMask == 1},
        {"upgradeActivate", active(ucm::kUsbExtendedFeatureUpgradeStagingV2 | ucm::kUsbExtendedFeatureUpgradeActivationV2) && c.activeUpgradeTargetMask == 1}};
}
