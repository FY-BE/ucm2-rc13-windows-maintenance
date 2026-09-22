#include "dashboard_poll_worker.h"
#include "upgrade_package_check.h"
#include "usb_extended_wire_v2.h"
#include "usb_runtime_parameter_edit_v9.h"
#include "poll_schedule.h"

#include <QMutexLocker>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimer>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <algorithm>
#include <limits>

DashboardPollWorker::DashboardPollWorker(
    ucm::ConfigurationSession *session, QMutex *sessionMutex,
    QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_sessionMutex(sessionMutex)
{
}

void DashboardPollWorker::poll(bool includeWaveform)
{
    DashboardPollResult result;
    result.waveformRequested = includeWaveform;
    if (m_session == nullptr || m_sessionMutex == nullptr) {
        result.telemetry.message = QStringLiteral("后台采集器尚未初始化。");
        emit completed(result);
        return;
    }

    {
        QMutexLocker lock(m_sessionMutex);
        result.productCommissioned = m_session->productOperationsCommissioned();
        result.discovery = m_session->usbExtendedDiscovery();
        result.transport = m_session->transportInfo();
        if (result.transport.realUsbOpened
                && result.transport.armReceiverContacted) {
            const bool product = m_session->usbExtendedDiscovery().capabilities.protocolRevision == 9;
            result.telemetry = m_session->readTelemetrySnapshot();
            const auto afterTelemetry = m_session->transportInfo();
            const bool stillConnected = afterTelemetry.realUsbOpened && afterTelemetry.armReceiverContacted;
            if (stillConnected && product && (!m_productAge.isValid()
                    || m_productAge.elapsed() >= ucm::ui::productStateIntervalMs)) {
                m_product = m_session->readProductState();
                m_productAge.start();
            }
            result.product = m_product;
            if (stillConnected && (!m_runtimeAge.isValid()
                    || m_runtimeAge.elapsed() >= ucm::ui::runtimeStatusIntervalMs)) {
                m_cachedRuntime = m_session->readRuntimeStatus();
                m_cachedCompound = m_session->readCompositeRuntimeStatus(); m_runtimeAge.restart();
            }
            result.runtime = m_cachedRuntime;
            result.compoundRuntime = m_cachedCompound;
            if (stillConnected && (!m_runtimeProgressAge.isValid()
                    || m_runtimeProgressAge.elapsed() >= 200)) {
                m_cachedRuntimeProgress = m_session->readRuntimeProgressV9();
                m_runtimeProgressAge.restart();
            }
            result.runtimeProgress = m_cachedRuntimeProgress;
            if (stillConnected && result.discovery.catalogReady
                && (result.discovery.capabilities.activeFeatureMask
                    & ucm::kUsbExtendedFeatureConfigV2) != 0U
                && (!m_runtimeConfigAge.isValid()
                    || m_runtimeConfigAge.elapsed() >= ucm::ui::runtimeConfigIntervalMs)) {
                m_cachedRuntimeConfig = m_session->readRuntimeConfigV9(
                    ucm::UsbRuntimeConfigQueryActiveV9);
                m_runtimeConfigAge.restart();
            }
            result.runtimeConfig = m_cachedRuntimeConfig;
            if (product) result.product.pairedInputStatus = result.telemetry.pairedInputStatus;
            result.diagnosticWaveformCaptureSupported =
                m_session->supportsWaveformViewport();
            if (includeWaveform)
                result.waveform = m_session->readWaveformSnapshot();
            result.productCommissioned = m_session->productOperationsCommissioned();
            result.discovery = m_session->usbExtendedDiscovery();
            result.transport = m_session->transportInfo();
        } else {
            m_product = {};
            m_productAge.invalidate();
            m_runtimeAge.invalidate(); m_runtimeConfigAge.invalidate(); m_runtimeProgressAge.invalidate();
            m_cachedRuntime = {}; m_cachedCompound = {}; m_cachedRuntimeConfig = {}; m_cachedRuntimeProgress = {};
            result.telemetry.message = QStringLiteral("真实USB尚未连接。");
        }
    }
    const bool connected = result.transport.realUsbOpened && result.transport.armReceiverContacted;
    if (!connected) {
        if (m_wasConnected) cancelProductOperation();
        m_product = {}; m_productAge.invalidate(); m_runtimeAge.invalidate();
        m_cachedRuntime = {}; m_cachedCompound = {}; m_cachedRuntimeConfig = {}; m_cachedRuntimeProgress = {};
        result.product = {}; result.runtime = {}; result.runtimeConfig = {};
        result.compoundRuntime = {}; result.telemetry = {};
        result.telemetry.message = QStringLiteral("真实USB尚未连接。");
    }
    if (connected && !m_wasConnected && m_queryAfterReconnect) {
        m_queryAfterReconnect = false;
        QTimer::singleShot(0, this, &DashboardPollWorker::queryProductOperation);
    }
    m_wasConnected = connected;
    emit completed(result);
}

void DashboardPollWorker::readLatestWaveform()
{
    DashboardWaveformResult result;
    if (!m_session || !m_sessionMutex) {
        result.message = QStringLiteral("后台波形读取器尚未初始化。");
    } else {
        QMutexLocker lock(m_sessionMutex);
        result.supported = m_session->supportsWaveformViewport();
        result.waveform = m_session->readWaveformSnapshot();
        result.message = result.waveform.message;
    }
    emit waveformReadCompleted(result);
}

void DashboardPollWorker::readWaveformViewport(
    quint32 sliceOffset, quint64 expectedGeneration)
{
    constexpr quint32 kRetainedSamples = 8192U;
    constexpr quint32 kWindowSamples = 2048U;
    DashboardWaveformResult result;
    result.viewportRead = true;
    result.sliceOffset = sliceOffset;
    if (!m_session || !m_sessionMutex) {
        result.message = QStringLiteral("后台诊断单次采集器尚未初始化。");
        emit waveformReadCompleted(result);
        return;
    }
    if (sliceOffset > kRetainedSamples - kWindowSamples) {
        result.message = QStringLiteral(
            "切片偏移越界：2048点窗口偏移必须在0到6144。");
        emit waveformReadCompleted(result);
        return;
    }
    QMutexLocker lock(m_sessionMutex);
    result.supported = m_session->supportsWaveformViewport();
    if (!result.supported) {
        result.message = QStringLiteral(
            "未发送：当前transport不支持revision 9消息7波形切片。");
        emit waveformReadCompleted(result);
        return;
    }
    ucm::WaveformViewportRequest request;
    request.expectedGeneration = expectedGeneration;
    request.sliceOffset = sliceOffset;
    request.sampleCount = kWindowSamples;
    const auto viewport = m_session->readWaveformViewport(request);
    result.supported = viewport.supported;
    result.message = viewport.message;
    result.waveform = viewport.waveform;
    emit waveformReadCompleted(result);
}

void DashboardPollWorker::runtimeAction(quint32 action,
                                        quint64 expectedGeneration)
{
    ucm::UsbRuntimeActionResultV9 result;
    if (!m_session || !m_sessionMutex) {
        result.message = QStringLiteral("后台运行动作执行器尚未初始化。");
    } else {
        QMutexLocker lock(m_sessionMutex);
        result = m_session->executeRuntimeActionV9(action, expectedGeneration);
        m_runtimeProgressAge.invalidate();
    }
    emit runtimeActionCompleted(result);
}

void DashboardPollWorker::runtimeBurstOperation(int operation, int cycles)
{
    ucm::UsbRuntimeConfigOperationResultV9 result;
    if (!m_session || !m_sessionMutex) {
        result.message = QStringLiteral("后台运行参数执行器尚未初始化。");
        emit runtimeBurstOperationCompleted(result);
        return;
    }
    QMutexLocker lock(m_sessionMutex);
    const auto active = m_session->readRuntimeConfigV9(
        ucm::UsbRuntimeConfigQueryActiveV9);
    if (!active.success) {
        emit runtimeBurstOperationCompleted(active);
        return;
    }
    ucm::UsbRuntimeConfigObjectV9 candidate = active.receipt.activeConfiguration;
    candidate.transactionId = QRandomGenerator::global()->generate64();
    if (candidate.transactionId == 0U) candidate.transactionId = 1U;
    candidate.baseGeneration = active.receipt.activeGeneration;
    if (operation == ucm::UsbRuntimeConfigSaveStartupV9) {
        candidate.operation = ucm::UsbRuntimeConfigSaveStartupV9;
        candidate.candidateGeneration = candidate.baseGeneration;
        candidate.changedGroupMask = 0U;
    } else if (operation == ucm::UsbRuntimeConfigApplyV9
               || operation == ucm::UsbRuntimeConfigValidateV9) {
        if (cycles < 1 || cycles > 8) {
            result.message = QStringLiteral("burst周期数必须为1到8。");
            emit runtimeBurstOperationCompleted(result);
            return;
        }
        quint16 burstGroup = 0U;
        for (const auto &descriptor : m_session->usbExtendedDiscovery().parameters)
            if (descriptor.fieldId == ucm::UsbParameterTxBurstCyclesV2)
                burstGroup = descriptor.groupId;
        if (burstGroup == 0U || burstGroup > 63U) {
            result.message = QStringLiteral("参数目录未提供burst字段所属配置组。");
            emit runtimeBurstOperationCompleted(result);
            return;
        }
        candidate.operation = static_cast<quint32>(operation);
        candidate.candidateGeneration = candidate.baseGeneration + 1U;
        candidate.changedGroupMask = 1ULL << (burstGroup - 1U);
        candidate.txBurstCycles = static_cast<quint32>(cycles);
    } else {
        result.message = QStringLiteral("未知运行参数操作码。");
        emit runtimeBurstOperationCompleted(result);
        return;
    }
    result = m_session->submitRuntimeConfigV9(candidate);
    if (result.success
        && result.receipt.kind == ucm::UsbRuntimeConfigReceiptAcceptedV9) {
        result = m_session->readRuntimeConfigV9(
            ucm::UsbRuntimeConfigQueryTransactionV9,
            candidate.transactionId);
    }
    if (result.success) {
        m_cachedRuntimeConfig = result;
        m_runtimeConfigAge.restart();
    }
    emit runtimeBurstOperationCompleted(result);
}

void DashboardPollWorker::runtimeParameterOperation(
    int operation, const QVariantList &serializedEdits)
{
    ucm::UsbRuntimeConfigOperationResultV9 result;
    if (!m_session || !m_sessionMutex) {
        result.message = QStringLiteral("后台运行参数执行器尚未初始化。");
        emit runtimeParameterOperationCompleted(result);
        return;
    }
    QMutexLocker lock(m_sessionMutex);
    const auto active = m_session->readRuntimeConfigV9(
        ucm::UsbRuntimeConfigQueryActiveV9);
    if (!active.success) {
        emit runtimeParameterOperationCompleted(active);
        return;
    }
    ucm::UsbRuntimeConfigObjectV9 candidate =
        active.receipt.activeConfiguration;
    candidate.transactionId = QRandomGenerator::global()->generate64();
    if (candidate.transactionId == 0U) candidate.transactionId = 1U;
    candidate.baseGeneration = active.receipt.activeGeneration;
    QVector<ucm::UsbRuntimeParameterEditV9> edits;
    quint64 changedGroups = 0U;
    QString error;

    if (operation == ucm::UsbRuntimeConfigSaveStartupV9) {
        if (!serializedEdits.isEmpty()) {
            result.message = QStringLiteral(
                "SaveStartup只保存ARM当前活动配置，不接受本地候选。");
            emit runtimeParameterOperationCompleted(result);
            return;
        }
        candidate.operation = ucm::UsbRuntimeConfigSaveStartupV9;
        candidate.candidateGeneration = candidate.baseGeneration;
        candidate.changedGroupMask = 0U;
    } else if (operation == ucm::UsbRuntimeConfigApplyV9) {
        for (const QVariant &serialized : serializedEdits) {
            const QVariantMap map = serialized.toMap();
            bool fieldOk = false, indexOk = false, valueOk = false;
            const quint32 fieldId = map.value(QStringLiteral("fieldId"))
                .toString().toUInt(&fieldOk);
            const int elementIndex = map.value(QStringLiteral("elementIndex"))
                .toString().toInt(&indexOk);
            const qint64 value = map.value(QStringLiteral("value"))
                .toString().toLongLong(&valueOk);
            if (!fieldOk || !indexOk || !valueOk) {
                result.message = QStringLiteral("运行参数候选不是完整整数编辑。");
                emit runtimeParameterOperationCompleted(result);
                return;
            }
            const auto &parameters = m_session->usbExtendedDiscovery().parameters;
            const auto descriptor = std::find_if(
                parameters.cbegin(), parameters.cend(),
                [fieldId](const ucm::UsbParameterDescriptorV2 &item) {
                    return item.fieldId == fieldId;
                });
            if (descriptor == parameters.cend()) {
                result.message = QStringLiteral("参数目录缺少字段%1。").arg(fieldId);
                emit runtimeParameterOperationCompleted(result);
                return;
            }
            const ucm::UsbRuntimeParameterEditV9 edit {
                fieldId, elementIndex, value};
            qint64 currentValue = 0;
            if (!ucm::usbRuntimeParameterValueV9(
                    candidate, fieldId, elementIndex, &currentValue, &error)
                || !ucm::applyUsbRuntimeParameterEditV9(
                    &candidate, *descriptor,
                    m_session->usbExtendedDiscovery()
                        .capabilities.activeConfigGroupMask,
                    edit, &error)) {
                result.message = error;
                emit runtimeParameterOperationCompleted(result);
                return;
            }
            if (currentValue != value) {
                edits.push_back(edit);
                changedGroups |= 1ULL << (descriptor->groupId - 1U);
            }
        }
        if (edits.isEmpty()) {
            result.message = QStringLiteral("运行参数候选与活动配置一致。");
            emit runtimeParameterOperationCompleted(result);
            return;
        }
        candidate.operation = ucm::UsbRuntimeConfigApplyV9;
        candidate.candidateGeneration = candidate.baseGeneration + 1U;
        candidate.changedGroupMask = changedGroups;
    } else {
        result.message = QStringLiteral("未知运行参数操作码。");
        emit runtimeParameterOperationCompleted(result);
        return;
    }

    result = m_session->submitRuntimeConfigV9(candidate);
    if (result.success
        && result.receipt.kind == ucm::UsbRuntimeConfigReceiptAcceptedV9) {
        result = m_session->readRuntimeConfigV9(
            ucm::UsbRuntimeConfigQueryTransactionV9,
            candidate.transactionId);
    }
    if (result.success
        && result.receipt.kind == ucm::UsbRuntimeConfigReceiptAppliedV9
        && !ucm::verifyUsbRuntimeParameterReadbackV9(
            result.receipt, edits, &error)) {
        result.success = false;
        result.message = error;
    }
    if (result.success
        && result.receipt.kind == ucm::UsbRuntimeConfigReceiptSavedStartupV9) {
        const auto startup = m_session->readRuntimeConfigV9(
            ucm::UsbRuntimeConfigQueryStartupV9);
        if (!startup.success
            || !ucm::usbRuntimeConfigurationValuesEqualV9(
                result.receipt.activeConfiguration,
                startup.receipt.activeConfiguration)) {
            result.success = false;
            result.message = startup.success
                ? QStringLiteral("SaveStartup后A/B启动配置回读不一致。")
                : startup.message;
        }
    }
    if (result.success) {
        m_cachedRuntimeConfig = result;
        m_runtimeConfigAge.restart();
    }
    emit runtimeParameterOperationCompleted(result);
}

void DashboardPollWorker::applyPreparedConfiguration()
{
    ConfigurationApplyResult result;
    if (m_session == nullptr || m_sessionMutex == nullptr) {
        result.message = QStringLiteral("后台配置执行器尚未初始化。");
        emit configurationApplied(result);
        return;
    }

    {
        QMutexLocker lock(m_sessionMutex);
        result.committed = m_session->commitRam();
        result.message = m_session->lastMessage();
        if (result.committed) {
            result.confirmed = m_session->confirmReadback();
            result.message = m_session->lastMessage();
        }
        if (result.confirmed) {
            result.activeIdentity = ucm::identityFor(
                m_session->active());
        }
        result.failurePreservesActive =
            m_session->transportInfo().failurePreservesActive;
    }
    emit configurationApplied(result);
}

void DashboardPollWorker::controlAuthority(
    ControlAuthorityOperation operation, quint32 requestedMode)
{
    ControlAuthorityOperationResult completed;
    completed.operation = operation;
    completed.requestedMode = requestedMode;
    if (m_session == nullptr || m_sessionMutex == nullptr) {
        completed.result.message = QStringLiteral(
            "后台控制权执行器尚未初始化。");
        emit controlAuthorityCompleted(completed);
        return;
    }

    {
        QMutexLocker lock(m_sessionMutex);
        switch (operation) {
        case ControlAuthorityOperation::Refresh:
            completed.result = m_session->readControlAuthorityState();
            break;
        case ControlAuthorityOperation::SwitchMode:
            completed.result = m_session->switchControlMode(requestedMode);
            break;
        case ControlAuthorityOperation::RenewLease:
            completed.result = m_session->renewControlLease();
            break;
        case ControlAuthorityOperation::ResumeHost:
            completed.result = m_session->resumeHostControl();
            break;
        }
        completed.discovery = m_session->usbExtendedDiscovery();
        completed.productCommissioned = m_session->productOperationsCommissioned();
    }
    emit controlAuthorityCompleted(completed);
}

void DashboardPollWorker::reconnectTransport()
{
    cancelProductExport();
    cancelProductOperation();
    m_wasConnected = false;
    if (m_session && m_sessionMutex) {
        QMutexLocker lock(m_sessionMutex);
        m_product = {};
        m_productAge.invalidate();
        m_runtimeAge.invalidate(); m_runtimeConfigAge.invalidate();
        m_cachedRuntime = {}; m_cachedCompound = {}; m_cachedRuntimeConfig = {};
        m_session->reconnectTransport();
    }
    poll(false);
}

void DashboardPollWorker::disconnectTransport()
{
    cancelProductExport();
    cancelProductOperation();
    m_wasConnected = false;
    if (m_session && m_sessionMutex) {
        QMutexLocker lock(m_sessionMutex);
        m_session->disconnectTransport();
        m_product = {};
        m_productAge.invalidate();
        m_runtimeAge.invalidate(); m_runtimeConfigAge.invalidate();
        m_cachedRuntime = {}; m_cachedCompound = {}; m_cachedRuntimeConfig = {};
    }
    poll(false);
}

void DashboardPollWorker::readLogSources(quint64 token)
{
    ucm::DeviceLogListResult result;
    if (m_session && m_sessionMutex) {
        QMutexLocker lock(m_sessionMutex);
        result = m_session->readLogSources();
    }
    emit logSourcesCompleted(token, result);
}

void DashboardPollWorker::readLogChunk(quint64 token, quint32 sourceId, quint64 snapshotId)
{
    ucm::DeviceLogChunkResult result;
    if (m_session && m_sessionMutex) {
        QMutexLocker lock(m_sessionMutex);
        result = m_session->readLogChunk(sourceId, 0, 4096, snapshotId);
    }
    emit logChunkCompleted(token, result);
}

void DashboardPollWorker::exportProductPackage(const QString &directory, const QByteArray &csv)
{
    if (m_exportActive || !m_session || !m_sessionMutex) return;
    m_exportActive = true;
    m_exportDirectory = directory;
    m_exportIndex = 0;
    m_exportFiles = {{QStringLiteral("arm-force-window.csv"), csv}};
    m_exportErrors = {};
    m_exportLogMetadata = {};
    m_exportSources.clear();
    m_download = {};
    m_exportManifest = {
        {QStringLiteral("kind"), QStringLiteral("ucm-product-w1-read-only")},
        {QStringLiteral("captured_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("formal_recalculation"), false}
    };
    QFile executable(QCoreApplication::applicationFilePath());
    QCryptographicHash executableHash(QCryptographicHash::Sha256);
    const bool identityRead = executable.open(QIODevice::ReadOnly) && executableHash.addData(&executable);
    QJsonObject softwareIdentity {
        {QStringLiteral("product"), QStringLiteral("UCM Windows revision 9 candidate")},
        {QStringLiteral("application_name"), QCoreApplication::applicationName()},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("executable_sha256"), identityRead ? QString::fromLatin1(executableHash.result().toHex()) : QString()},
        {QStringLiteral("physical_commissioning_verified"), false}
    };
    if (!identityRead) m_exportErrors.append(QStringLiteral("software-identity: cannot hash running executable"));
    m_exportFiles.push_back({QStringLiteral("software-identity.json"), QJsonDocument(softwareIdentity).toJson()});
    {
        QMutexLocker lock(m_sessionMutex);
        const auto product = m_session->readProductState();
        QJsonObject state {
            {QStringLiteral("success"), product.success},
            {QStringLiteral("message"), product.message},
            {QStringLiteral("device_model"), product.deviceModel},
            {QStringLiteral("input_policy"), product.inputPolicy},
            {QStringLiteral("paired_input_status"), product.pairedInputStatus}
        };
        m_exportFiles.push_back({QStringLiteral("product-state.json"), QJsonDocument(state).toJson()});
        if (!product.success) m_exportErrors.append(QStringLiteral("product-state: ") + product.message);
        const auto capabilities = m_session->readUsbExtendedCapabilitiesObject();
        if (capabilities.success)
            m_exportFiles.push_back({QStringLiteral("capabilities.bin"), capabilities.data});
        else m_exportErrors.append(QStringLiteral("capabilities: ") + capabilities.message);
        const auto runtimeStatus = m_session->readRuntimeStatusObject();
        if (runtimeStatus.success)
            m_exportFiles.push_back({QStringLiteral("runtime-status.bin"), runtimeStatus.data});
        else m_exportErrors.append(QStringLiteral("runtime-status: ") + runtimeStatus.message);
        const auto runtimeProgress = m_session->readRuntimeProgressV9();
        if (runtimeProgress.success)
            m_exportFiles.push_back({QStringLiteral("runtime-progress-256.bin"), runtimeProgress.raw});
        else m_exportErrors.append(QStringLiteral("runtime-progress: ") + runtimeProgress.message);
        const auto compoundStatus = m_session->readCompositeRuntimeStatusObject();
        if (compoundStatus.success)
            m_exportFiles.push_back({QStringLiteral("compound-runtime-status.bin"), compoundStatus.data});
        else m_exportErrors.append(QStringLiteral("compound-runtime-status: ") + compoundStatus.message);
        const auto telemetry = m_session->readTelemetryObject();
        if (telemetry.success)
            m_exportFiles.push_back({QStringLiteral("formal-result.bin"), telemetry.data});
        else m_exportErrors.append(QStringLiteral("formal-result: ") + telemetry.message);
        // The waveform is explicitly optional.  Preserve it when the active
        // ARM build exposes one, but do not turn an otherwise complete log
        // package into READS_INCOMPLETE when it is unavailable.
        const auto waveform = m_session->readWaveformObject();
        if (waveform.success)
            m_exportFiles.push_back({QStringLiteral("waveform-snapshot.bin"), waveform.data});
        const auto list = m_session->readLogSources();
        m_exportSources = list.sources;
        if (!list.success) m_exportErrors.append(QStringLiteral("log-catalog: ") + list.message);
        m_exportFiles.push_back({QStringLiteral("transport-evidence.json"), QJsonDocument(m_session->evidence()).toJson()});
    }
    QTimer::singleShot(0, this, &DashboardPollWorker::exportNextChunk);
}

void DashboardPollWorker::exportNextChunk()
{
    if (!m_exportActive) return;
    if (m_exportIndex >= m_exportSources.size()) { finishProductExport(); return; }
    const auto &source = m_exportSources.at(m_exportIndex);
    if (!m_download.active()) {
        if (!m_download.begin(source)) {
            m_exportErrors.append(QStringLiteral("log %1: %2").arg(source.sourceId).arg(m_download.error()));
            ++m_exportIndex;
            QTimer::singleShot(0, this, &DashboardPollWorker::exportNextChunk);
            return;
        }
    }
    ucm::DeviceLogChunkResult chunk;
    {
        QMutexLocker lock(m_sessionMutex);
        chunk = m_session->readLogChunk(source.sourceId, m_download.offset(),
            m_download.nextBytes(), source.snapshotId);
    }
    if (!m_download.accept(chunk)) {
        m_exportErrors.append(QStringLiteral("log %1: %2").arg(source.sourceId).arg(m_download.error()));
        m_download = {};
        // Directory indices cannot be reused after an identity conflict.
        {
            QMutexLocker lock(m_sessionMutex);
            const auto refreshed = m_session->readLogSources();
            m_exportManifest.insert(QStringLiteral("catalog_refreshed_after_failure"), refreshed.success);
            if (!refreshed.success) m_exportErrors.append(QStringLiteral("catalog refresh: ") + refreshed.message);
        }
        if (m_exportIndex + 1 < m_exportSources.size())
            m_exportErrors.append(QStringLiteral("Remaining files skipped after catalog identity changed; export again with the refreshed directory."));
        m_exportIndex = m_exportSources.size();
    } else if (m_download.complete()) {
        const QString fileName = QStringLiteral("device-log-%1.bin").arg(source.sourceId);
        m_exportFiles.push_back({fileName, m_download.data()});
        m_exportLogMetadata.append(QJsonObject {
            {QStringLiteral("file"), fileName},
            {QStringLiteral("device_name"), source.name},
            {QStringLiteral("snapshot_id"), QString::number(source.snapshotId)},
            {QStringLiteral("mutable_snapshot"), source.mutableFile},
            {QStringLiteral("sha256"), QString::fromLatin1(m_download.localSha256().toHex())}
        });
        m_download = {};
        ++m_exportIndex;
    }
    emit productPackageProgress(QStringLiteral("诊断日志 %1/%2；后台分块读取中")
        .arg(m_exportIndex).arg(m_exportSources.size()));
    QTimer::singleShot(0, this, &DashboardPollWorker::exportNextChunk);
}

void DashboardPollWorker::finishProductExport()
{
    m_exportManifest.insert(QStringLiteral("logs"), m_exportLogMetadata);
    m_exportManifest.insert(QStringLiteral("errors"), m_exportErrors);
    m_exportManifest.insert(QStringLiteral("all_requested_reads_succeeded"), m_exportErrors.isEmpty());
    m_exportFiles.push_back({QStringLiteral("read-errors.json"), QJsonDocument(m_exportErrors).toJson()});
    if (!m_exportErrors.isEmpty()) m_exportFiles.push_back({QStringLiteral("READS_INCOMPLETE"), QByteArray("Some requested device reads failed; inspect read-errors.json.\n")});
    auto result = ucm::writeDiagnosticPackage(m_exportDirectory,
        QStringLiteral("ucm-product-w1-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')),
        m_exportManifest, m_exportFiles);
    if (result.success && !m_exportErrors.isEmpty()) {
        result.success = false;
        result.message = QStringLiteral("部分读取失败；已保留诊断包与错误清单，不能视为完整日志归档");
    }
    m_exportActive = false;
    m_exportFiles.clear();
    emit productPackageCompleted(result);
}

void DashboardPollWorker::cancelProductExport()
{
    if (!m_exportActive) return;
    m_exportActive = false;
    m_download.cancel(QStringLiteral("连接已改变"));
    m_exportFiles.clear();
    m_exportSources.clear();
    emit productPackageCompleted({false, QStringLiteral("连接已改变，诊断导出已取消；未保存未校验日志"), {}});
}

void DashboardPollWorker::reportProductOperation(const ucm::ProductOperationResult &result, bool busy)
{
    m_productOperationActive = busy;
    auto presentation = result;
    if (m_upgradeOperation) {
        QString phase = m_upgradePhase;
        if (result.outcome == "unresolved") phase = "unresolved";
        else if (result.outcome == "failed" || result.outcome == "denied") phase = "failed";
        else if (result.ok) {
            const int state = result.fields.value("state").toInt();
            if (m_activationNeedsQuery) phase = "reboot_query";
            else if (state == 1) phase = "transferring";
            else if (state == 2) phase = "verifying";
            else if (state == 3) phase = "staged";
            else if (state == 4) phase = "activating";
            else if (state == 5 && result.outcome == "succeeded") phase = "complete";
            else if (state >= 6) phase = "failed";
        }
        presentation.fields.insert("phase", phase);
    }
    if (presentation.message.isEmpty()) presentation.message = presentation.outcome == "succeeded"
        ? QStringLiteral("ARM已确认事务成功") : QStringLiteral("ARM事务状态：%1").arg(presentation.outcome);
    m_lastProductOperation = presentation;
    if (presentation.outcome == "unresolved") m_queryAfterReconnect = true;
    else if (presentation.outcome == "succeeded" || presentation.outcome == "failed") m_queryAfterReconnect = false;
    if (!m_upgradeOperation && (presentation.outcome == "succeeded" || presentation.outcome == "failed")) {
        m_productAge.invalidate(); QTimer::singleShot(0, this, [this] { poll(false); });
    }
    emit productOperationUpdated(presentation, busy);
}

void DashboardPollWorker::readCalibrationParameters()
{
    QVariantMap report {{QStringLiteral("busy"), false},
        {QStringLiteral("outcome"), QStringLiteral("failed")},
        {QStringLiteral("message"), QStringLiteral("USB配置回读不可用")}};
    if (!m_session || !m_sessionMutex || m_productOperationActive) {
        emit calibrationParametersRead(report);
        return;
    }
    {
        QMutexLocker lock(m_sessionMutex);
        m_product = m_session->readProductState();
        m_productAge.restart();
    }
    const auto device = m_product.deviceModel;
    const bool success = m_product.success
        && device.value(QStringLiteral("available")).toBool();
    report.insert(QStringLiteral("outcome"), success ? QStringLiteral("succeeded") : QStringLiteral("failed"));
    report.insert(QStringLiteral("message"), success ? QStringLiteral("已通过USB回读ARM当前运行与开机保存参数") : m_product.message);
    report.insert(QStringLiteral("readUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    for (const auto *key : {"active_document", "startup_document", "active_identity", "startup_identity"})
        report.insert(QString::fromLatin1(key), device.value(QString::fromLatin1(key)).toObject().toVariantMap());
    auto active = device.value(QStringLiteral("active_document")).toObject();
    auto startup = device.value(QStringLiteral("startup_document")).toObject();
    for (auto *document : {&active, &startup}) {
        document->remove(QStringLiteral("configuration_generation"));
        document->remove(QStringLiteral("device_model_config_id_sha256"));
        document->remove(QStringLiteral("system_package_id_sha256"));
    }
    report.insert(QStringLiteral("parametersMatch"), success && !active.isEmpty() && active == startup);
    emit calibrationParametersRead(report);
}

void DashboardPollWorker::submitProductDocument(int domain, int operation, const QByteArray &json, bool authorized,
                                                const QString &expectedActiveId)
{
    if (m_productOperationActive || !m_session || !m_sessionMutex) return;
    if (domain < 0 || domain > 1 || operation < 1 || operation > 3) {
        emit productOperationUpdated({false, QStringLiteral("failed"), QStringLiteral("配置域或操作编号无效；未发送USB请求"), {}}, false);
        return;
    }
    if (m_activationNeedsQuery || m_lastProductOperation.outcome == "unresolved") {
        queryProductOperation(); return;
    }
    ucm::ProductOperationResult result;
    m_upgradeOperation = false;
    const auto reject = [&](const QString &message) {
        reportProductOperation({false, QStringLiteral("failed"), message, {}}, false);
    };
    {
        QMutexLocker lock(m_sessionMutex);
        if (!authorized || !m_session->productOperationsCommissioned()) {
            reject(QStringLiteral("当前设备控制权/配置能力不可用或未确认本次操作；未发送配置")); return;
        }
        const auto product = m_session->readProductState();
        const auto target = domain == 0 ? product.deviceModel : product.inputPolicy;
        const auto state = target.value(QStringLiteral("state")).toObject();
        const auto authority = m_session->readControlAuthorityState();
        if (!product.success || !target.value(QStringLiteral("available")).toBool() || !authority.success) {
            reject(QStringLiteral("配置或ARM时间/控制权读取失败；未提交")); return;
        }
        if (!expectedActiveId.isEmpty()) {
            const QJsonObject expected = QJsonDocument::fromJson(expectedActiveId.toUtf8()).object();
            if (expected.isEmpty()
                || state.value(QStringLiteral("activeId")).toString() != expected.value(QStringLiteral("identitySha256")).toString()
                || state.value(QStringLiteral("activeGeneration")).toString() != expected.value(QStringLiteral("generation")).toString()
                || state.value(QStringLiteral("bootId")).toString() != expected.value(QStringLiteral("bootId")).toString()
                || state.value(QStringLiteral("authorityGeneration")).toString() != expected.value(QStringLiteral("authorityGeneration")).toString()
                || state.value(QStringLiteral("sessionId")).toString() != expected.value(QStringLiteral("sessionId")).toString()) {
                reject(QStringLiteral("标定以来ARM活动配置或会话身份已改变；未提交，请重新核对")); return;
            }
        }
        ucm::productv9::ConfigWriteRequest request;
        request.domain = domain == 0 ? ucm::productv9::Domain::DeviceModel : ucm::productv9::Domain::InputPolicy;
        request.operation = operation;
        request.bootId = state.value(QStringLiteral("bootId")).toString().toULongLong();
        request.authorityGeneration = state.value(QStringLiteral("authorityGeneration")).toString().toULongLong();
        request.sessionId = state.value(QStringLiteral("sessionId")).toString().toULongLong();
        if (!m_requestCounter) m_requestCounter = QRandomGenerator::global()->generate64() >> 1;
        request.requestId = ++m_requestCounter;
        request.entryId = ++m_requestCounter;
        request.expectedCurrentId = QByteArray::fromHex(state.value(QStringLiteral("activeId")).toString().toLatin1());
        request.systemSha256 = QByteArray::fromHex(state.value(QStringLiteral("systemSha256")).toString().toLatin1());
        request.createdMonotonicNs = authority.state.publishedMonotonicNs;
        if (!request.createdMonotonicNs || request.createdMonotonicNs > std::numeric_limits<quint64>::max() - 5000000000ULL) {
            reject(QStringLiteral("ARM单调时间无效；未提交")); return;
        }
        request.expiresMonotonicNs = request.createdMonotonicNs + 5000000000ULL;
        if (operation == 2) {
            request.resultId = request.expectedCurrentId;
            request.payloadDigest = QByteArray::fromHex(state.value(QStringLiteral("activePayloadDigest")).toString().toLatin1());
        } else {
            QJsonParseError parseError;
            auto doc = QJsonDocument::fromJson(json, &parseError);
            if (!doc.isObject() || parseError.error != QJsonParseError::NoError) {
                reject(QStringLiteral("草稿必须是完整的产品JSON对象")); return;
            }
            auto object = doc.object();
            const quint64 generation = state.value(QStringLiteral("activeGeneration")).toString().toULongLong();
            if (generation >= quint64(std::numeric_limits<qint64>::max())) {
                reject(QStringLiteral("配置代数超过本客户端可精确处理范围")); return;
            }
            object.insert(QStringLiteral("configuration_generation"), qint64(generation + 1));
            object.insert(QStringLiteral("system_package_id_sha256"), QString::fromLatin1(request.systemSha256.toHex()));
            QString error;
            request.json = ucm::productv9::serializeDocument(request.domain, object, &error);
            if (request.json.isEmpty()) { reject(error); return; }
            if (!ucm::productv9::computeDocumentIdentity(request.domain, request.json, &request.resultId, &error)) {
                reject(error); return;
            }
            object.insert(domain == 0 ? QStringLiteral("device_model_config_id_sha256") : QStringLiteral("system_input_policy_id_sha256"),
                QString::fromLatin1(request.resultId.toHex()));
            request.json = ucm::productv9::serializeDocument(request.domain, object, &error);
            if (request.json.isEmpty()) { reject(error); return; }
            request.payloadDigest = QCryptographicHash::hash(request.json, QCryptographicHash::Sha256);
        }
        result = m_session->submitProductConfiguration(request, authorized);
    }
    m_upgradeOperation = false;
    m_operationPolls = 0;
    m_operationAge.restart();
    const bool pending = result.ok && result.outcome == QStringLiteral("pending");
    reportProductOperation(result, pending);
    if (pending) QTimer::singleShot(250, this, &DashboardPollWorker::pollProductOperation);
}

void DashboardPollWorker::finishOperationQuery(ucm::ProductOperationResult result, bool automatic)
{
    bool pending = result.ok && result.outcome == "pending";
    if (m_upgradeOperation && result.ok) {
        const int state = result.fields.value("state").toInt();
        if (state >= 5) { m_activationNeedsQuery = false; m_queryAfterReconnect = false; }
        if (state == 3) {
            if (m_activationNeedsQuery) {
                result.ok = false; result.outcome = "unresolved";
                result.message = QStringLiteral("激活后查询仍为暂存状态，结果未知；不会重发激活");
            } else result.message = QStringLiteral("包已验证并暂存；尚未激活，请单独确认激活");
            pending = false;
        }
        if (state == 1) pending = false; // Query never resumes upload automatically.
    }
    if (pending && (++m_operationPolls >= 120 || (m_operationAge.isValid() && m_operationAge.elapsed() >= 60000))) {
        result.ok = false; result.outcome = "unresolved";
        result.message = QStringLiteral("查询达到时限，事务仍待确认；未重发写入或激活"); pending = false;
    }
    reportProductOperation(result, automatic && pending);
    if (automatic && pending) QTimer::singleShot(250, this, &DashboardPollWorker::pollProductOperation);
}
void DashboardPollWorker::pollProductOperation()
{
    if (!m_productOperationActive || !m_session || !m_sessionMutex) return;
    ucm::ProductOperationResult result;
    { QMutexLocker lock(m_sessionMutex);
      result = m_upgradeOperation ? m_session->pollProductUpgrade() : m_session->pollProductConfiguration(++m_requestCounter); }
    finishOperationQuery(result, true);
}

void DashboardPollWorker::stageProductUpgrade(const QString &path, quint32 version, const QString &model, const QString &buildId, bool authorized)
{
    if (m_productOperationActive || !m_session || !m_sessionMutex) return;
    if (m_activationNeedsQuery || m_lastProductOperation.outcome == "unresolved") {
        queryProductOperation(); return;
    }
    m_upgradeOperation = true; m_upgradePhase = "checking";
    if (!authorized || !m_session->productOperationsCommissioned()) {
        reportProductOperation({false, QStringLiteral("failed"), QStringLiteral("当前设备升级能力/安全状态不可用或未确认；未发送升级数据"), {}}, false); return;
    }
    m_upgradeOperation = true; m_upgradePhase = "checking";
    m_upgradeFile.setFileName(path);
    if (!m_upgradeFile.open(QIODevice::ReadOnly) || m_upgradeFile.size() <= 0 || m_upgradeFile.size() > 16LL * 1024 * 1024) {
        m_upgradeFile.close();
        reportProductOperation({false, QStringLiteral("failed"), QStringLiteral("无法读取升级包或文件超过16MiB"), {}}, false); return;
    }
    m_upgradeOperation = true; m_activationNeedsQuery = false; m_upgradePhase = "checking";
    reportProductOperation({true, QStringLiteral("pending"), QStringLiteral("正在预检R2S U2P1升级包"), {}}, true);
    QByteArray activeBuildId, wholeSha; QString error;
    { QMutexLocker lock(m_sessionMutex);
      const auto discovery = m_session->usbExtendedDiscovery();
      if (discovery.available) activeBuildId = discovery.capabilities.buildId.toUtf8(); }
    if (!checkUpgradePackage(m_upgradeFile, version, model.toUtf8(), buildId.toUtf8(), activeBuildId, &wholeSha, &error) || !m_upgradeFile.seek(0)) {
        m_upgradeFile.close();
        reportProductOperation({false, QStringLiteral("failed"), error.isEmpty() ? QStringLiteral("无法定位升级文件") : error, {}}, false); return;
    }
    ucm::productv9::UpgradePackage package;
    if (!m_requestCounter) m_requestCounter = QRandomGenerator::global()->generate64() >> 1;
    package.transactionId = ++m_requestCounter;
    package.totalBytes = m_upgradeFile.size();
    package.packageVersion = version;
    package.deviceModel = model.toUtf8(); package.buildId = buildId.toUtf8();
    package.sha256 = wholeSha;
    ucm::ProductOperationResult result;
    {
        QMutexLocker lock(m_sessionMutex);
        result = m_session->beginProductUpgrade(package, authorized);
    }
    m_upgradeOperation = true;
    m_operationPolls = 0;
    m_operationAge.restart();
    const bool pending = result.ok && result.outcome == QStringLiteral("pending");
    reportProductOperation(result, pending);
    if (pending) QTimer::singleShot(0, this, &DashboardPollWorker::nextUpgradeChunk);
    else m_upgradeFile.close();
}

void DashboardPollWorker::nextUpgradeChunk()
{
    if (!m_productOperationActive || !m_upgradeFile.isOpen()) return;
    ucm::ProductOperationResult result;
    const bool atEnd = m_upgradeFile.atEnd();
    m_upgradePhase = atEnd ? "verifying" : "transferring";
    if (atEnd) reportProductOperation({true, QStringLiteral("pending"), QStringLiteral("ARM正在验证已传输包"), {}}, true);
    {
        QMutexLocker lock(m_sessionMutex);
        result = atEnd ? m_session->finalizeProductUpgrade()
            : m_session->writeProductUpgradeChunk(m_upgradeFile.read(m_upgradeChunkBytes));
    }
    bool pending = result.ok && result.outcome == QStringLiteral("pending");
    if (atEnd) m_upgradeFile.close();
    if (atEnd && result.fields.value(QStringLiteral("state")).toInt() == 3) {
        result.message = QStringLiteral("已完成包校验与暂存；请单独确认激活"); pending = false;
    }
    reportProductOperation(result, pending);
    if (pending) {
        if (atEnd) QTimer::singleShot(250, this, &DashboardPollWorker::pollProductOperation);
        else QTimer::singleShot(0, this, &DashboardPollWorker::nextUpgradeChunk);
    } else m_upgradeFile.close();
}

void DashboardPollWorker::activateProductUpgrade(bool authorized)
{
    if (m_productOperationActive || !m_session || !m_sessionMutex) return;
    if (m_activationNeedsQuery) { queryProductOperation(); return; }
    if (!authorized) { reportProductOperation({false, QStringLiteral("denied"), QStringLiteral("激活尚未确认"), {}}, false); return; }
    m_upgradeOperation = true; m_upgradePhase = "activating";
    reportProductOperation({true, QStringLiteral("pending"), QStringLiteral("正在请求ARM激活"), {}}, true);
    ucm::ProductOperationResult result;
    { QMutexLocker lock(m_sessionMutex); result = m_session->activateProductUpgrade(authorized); }
    m_operationPolls = 0; m_operationAge.restart();
    m_activationNeedsQuery = result.ok || result.outcome == "unresolved";
    if (result.ok) {
        result.outcome = "pending";
        result.message = QStringLiteral("已收到激活回执，正在额外只读查询确认终态");
        reportProductOperation(result, true);
        QTimer::singleShot(250, this, &DashboardPollWorker::pollProductOperation);
    } else reportProductOperation(result, false);
}

void DashboardPollWorker::cancelProductOperation()
{
    if (!m_productOperationActive && !m_upgradeFile.isOpen()
        && m_lastProductOperation.outcome != QStringLiteral("pending")
        && m_lastProductOperation.outcome != QStringLiteral("unresolved")) return;
    m_upgradeFile.close();
    const auto evidence = m_lastProductOperation.outcome == QStringLiteral("unresolved")
        ? m_lastProductOperation.fields
        : QJsonObject {{QStringLiteral("previousEvidence"), m_lastProductOperation.fields}};
    reportProductOperation({false, QStringLiteral("unresolved"), QStringLiteral("连接已改变，事务结果未知；重连后将只读查询原事务，未自动重发或激活"), evidence}, false);
}

void DashboardPollWorker::queryProductOperation()
{
    if (m_productOperationActive || !m_session || !m_sessionMutex) return;
    m_operationPolls = 0;
    m_operationAge.restart();
    ucm::ProductOperationResult result;
    { QMutexLocker lock(m_sessionMutex); result = m_upgradeOperation ? m_session->pollProductUpgrade() : m_session->pollProductConfiguration(++m_requestCounter); }
    finishOperationQuery(result, true);
}

void DashboardPollWorker::abortProductUpgrade(bool authorized)
{
    if (!m_session || !m_sessionMutex) return;
    m_upgradeFile.close();
    ucm::ProductOperationResult result;
    { QMutexLocker lock(m_sessionMutex); result = m_session->abortProductUpgrade(1, authorized); }
    if (result.ok && result.fields.value(QStringLiteral("state")).toInt() == 7)
        result.message = QStringLiteral("ARM已确认中止升级；未激活");
    reportProductOperation(result, false);
}
