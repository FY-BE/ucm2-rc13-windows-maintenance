#include "dashboard_poll_worker.h"

#include "mock_transport.h"
#include "usb_extended_wire_v2.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include <cstdio>
#include <memory>

namespace {

class SlowTransport final : public ucm::ConfigurationTransport {
public:
    bool productMode = false;
    bool commissioned = false;
    bool online = true;
    int productReads = 0;
    int productWrites = 0;
    int waveformReads = 0;
    bool conflictLog = false;
    int logChunks = 0;
    QStringList readOrder;
    int upgradeWrites = 0;
    int upgradeQueries = 0;
    bool recoverUpgrade = false;
    ucm::ProductOperationResult pollProductConfiguration(quint64) override {
        return {true, QStringLiteral("succeeded"), QStringLiteral("configuration confirmed"), {}};
    }
    bool productOperationsCommissioned() const override { return commissioned; }
    ucm::ProductOperationResult submitProductConfiguration(const ucm::productv9::ConfigWriteRequest &, bool) override {
        ++productWrites;
        return {true, QStringLiteral("succeeded"), QStringLiteral("submitted"), {}};
    }
    ucm::ProductOperationResult beginProductUpgrade(const ucm::productv9::UpgradePackage &, bool) override { ++upgradeWrites; return {}; }
    ucm::ProductOperationResult activateProductUpgrade(bool) override {
        ++upgradeWrites;
        return {false, QStringLiteral("unresolved"), QStringLiteral("lost activation receipt"), {}};
    }
    ucm::ProductOperationResult pollProductUpgrade() override {
        ++upgradeQueries;
        if (!recoverUpgrade) return {};
        const bool done = upgradeQueries >= 2;
        return {true, done ? QStringLiteral("succeeded") : QStringLiteral("pending"), {}, {{QStringLiteral("state"), done ? 5 : 4}}};
    }
    ucm::BinaryObjectResult readUsbExtendedCapabilitiesObject() override {
        return {true, QStringLiteral("fake validated capabilities"), QByteArray("capabilities")};
    }
    ucm::BinaryObjectResult readRuntimeStatusObject() override {
        return {true, QStringLiteral("fake runtime status"), QByteArray("runtime-status")};
    }
    ucm::UsbRuntimeProgressResultV9 readRuntimeProgressV9() override {
        ++m_runtimeProgressReads;
        ucm::UsbRuntimeProgressResultV9 result;
        result.success = true;
        result.progress.available = true;
        result.progress.generation = 1;
        result.raw = QByteArray(256, 'P');
        return result;
    }
    ucm::BinaryObjectResult readCompositeRuntimeStatusObject() override {
        return {true, QStringLiteral("fake compound status"), QByteArray("compound-status")};
    }
    ucm::BinaryObjectResult readTelemetryObject() override {
        return {true, QStringLiteral("fake formal result"), QByteArray("formal-result")};
    }
    ucm::BinaryObjectResult readWaveformObject() override {
        return {false, QStringLiteral("optional waveform unavailable"), {}};
    }
    ucm::DeviceLogListResult readLogSources() override {
        ucm::DeviceLogSource source;
        source.sourceId = 1; source.available = true; source.totalBytes = 70040;
        source.snapshotId = 22; source.fileIdentityCrc32 = 33;
        source.name = QStringLiteral("result-20260909-001.urs2");
        source.mutableFile = true;
        return {true, QStringLiteral("catalog"), {source}};
    }
    ucm::DeviceLogChunkResult readLogChunk(quint32 sourceId, quint64 offset, quint32 maximum, quint64 snapshotId) override {
        ++logChunks;
        ucm::DeviceLogChunkResult result;
        result.success = online; result.sourceId = sourceId; result.offset = offset;
        result.totalBytes = 70040; result.snapshotId = snapshotId;
        result.fileIdentityCrc32 = conflictLog && offset ? 44 : 33;
        result.data = QByteArray(int(qMin<quint64>(maximum, 70040-offset)), 'A');
        result.more = offset + quint64(result.data.size()) < result.totalBytes;
        return result;
    }
    ucm::UsbExtendedDiscoveryV2 usbExtendedDiscovery() const override {
        ucm::UsbExtendedDiscoveryV2 value;
        value.available = productMode;
        value.capabilities.protocolRevision = productMode ? 9 : 2;
        return value;
    }
    ucm::ProductReadState readProductState() override {
        ++productReads;
        readOrder.append(QStringLiteral("product"));
        ucm::ProductReadState value;
        value.supported = value.success = true;
        value.deviceModel = {{QStringLiteral("available"), true},
            {QStringLiteral("state"), QJsonObject{{QStringLiteral("activeId"), QString(64, QLatin1Char('a'))}}}};
        return value;
    }
    ucm::WaveformSnapshot readWaveformSnapshot() override {
        ++waveformReads;
        return {};
    }
    void disconnect() override { online = false; }
    void rejectNextReadback()
    {
        m_rejectReadback = true;
    }

    const ucm::Configuration &activeConfiguration() const override
    {
        return m_mock.activeConfiguration();
    }

    bool hasStagedConfiguration() const override
    {
        return m_mock.hasStagedConfiguration();
    }

    ucm::TransportInfo info() const override
    {
        ucm::TransportInfo result = m_mock.info();
        result.simulationOnly = false;
        result.realUsbOpened = online;
        result.armReceiverContacted = online;
        return result;
    }

    ucm::TransportResult stageRam(
        const ucm::Configuration &candidate) override
    {
        QThread::msleep(350);
        return m_mock.stageRam(candidate);
    }

    ucm::ReadbackResult readBackStaged() override
    {
        if (m_rejectReadback) {
            m_rejectReadback = false;
            return {false, {}, QStringLiteral(
                "ARM explicitly rejected; active configuration unchanged")};
        }
        return m_mock.readBackStaged();
    }

    ucm::TransportResult confirmReadback() override
    {
        QThread::msleep(350);
        return m_mock.confirmReadback();
    }

    void rollbackStagedRam() override { m_mock.rollbackStagedRam(); }

    ucm::ControlAuthorityResultV2 readControlAuthorityState() override
    {
        return {true, QStringLiteral("authority state"), m_authority, 0};
    }

    ucm::ControlAuthorityResultV2 switchControlMode(
        quint32 requestedMode) override
    {
        QThread::msleep(350);
        ++m_authority.generation;
        m_authority.appliedMode = requestedMode;
        m_authority.requestedMode = requestedMode;
        m_authority.phase = requestedMode == 2U ? 7U : 3U;
        m_authority.owner = requestedMode == 2U ? 3U : 2U;
        m_authority.hardwareActive = true;
        return {true, QStringLiteral("mode switched"), m_authority,
                m_authority.generation};
    }

    ucm::ControlAuthorityResultV2 renewControlLease() override
    {
        QThread::msleep(350);
        if (m_authority.appliedMode != 2U || m_authority.phase != 7U)
            return {false, QStringLiteral("not host active"), m_authority, 0};
        m_authority.hostLeaseDeadlineNs += 5000000000ULL;
        return {true, QStringLiteral("lease renewed"), m_authority,
                m_authority.generation + 100U};
    }

    ucm::ControlAuthorityResultV2 resumeHostControl() override
    {
        QThread::msleep(350);
        m_authority.appliedMode = 2U;
        m_authority.requestedMode = 2U;
        m_authority.phase = 7U;
        m_authority.owner = 3U;
        m_authority.hardwareActive = true;
        return {true, QStringLiteral("host resumed"), m_authority,
                m_authority.generation + 200U};
    }

    ucm::RuntimeStatusResultV2 readRuntimeStatus() override
    {
        ++m_runtimeReads;
        readOrder.append(QStringLiteral("runtime"));
        ucm::RuntimeStatusResultV2 result;
        result.success = true;
        result.status.available = true;
        result.status.flags = ucm::kUsbRuntimeStatusDaemonRunningV2
            | ucm::kUsbRuntimeStatusCaptureLoopActiveV2
            | ucm::kUsbRuntimeStatusHardwareActiveV2;
        result.status.runState = ucm::kUsbRuntimeActiveV2;
        result.status.controlMode = ucm::kUsbControlModeAutonomousV2;
        result.status.controlPhase =
            ucm::kUsbControlPhaseAutonomousTrackingV2;
        result.status.templateState = ucm::kUsbRuntimeTemplateBuildingV2;
        return result;
    }

    ucm::UsbCompositeRuntimeStatusResultV1
    readCompositeRuntimeStatus() override
    {
        ++m_compositeRuntimeReads;
        readOrder.append(QStringLiteral("compound"));
        ucm::UsbCompositeRuntimeStatusResultV1 result;
        result.success = true;
        result.code = ucm::UsbCompositeRuntimeDecodeCodeV1::Ok;
        result.snapshot.authoritativeContractBound = true;
        result.snapshot.available = true;
        result.snapshot.sourceBytesValidated = true;
        result.snapshot.runtimeChainClaimKnown = true;
        result.snapshot.runtimeChainClaim = false;
        return result;
    }

    ucm::TelemetrySnapshot readTelemetrySnapshot() override
    {
        ++m_telemetryReads;
        readOrder.append(QStringLiteral("telemetry"));
        ucm::TelemetrySnapshot result;
        result.message = QStringLiteral("status=7 / NOT_FOUND");
        return result;
    }

    int runtimeReads() const { return m_runtimeReads; }
    int compositeRuntimeReads() const { return m_compositeRuntimeReads; }
    int telemetryReads() const { return m_telemetryReads; }
    int runtimeProgressReads() const { return m_runtimeProgressReads; }

private:
    ucm::MockTransport m_mock;
    bool m_rejectReadback = false;
    int m_runtimeReads = 0;
    int m_compositeRuntimeReads = 0;
    int m_telemetryReads = 0;
    int m_runtimeProgressReads = 0;
    ucm::ControlAuthorityStateV2 m_authority {
        true, 1U, 1U, 3U, 2U, 0U, true, 0,
        1U, 0U, 0U, 0U, 0U
    };
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    qRegisterMetaType<ConfigurationApplyResult>();
    qRegisterMetaType<DashboardPollResult>();
    qRegisterMetaType<ControlAuthorityOperation>();
    qRegisterMetaType<ControlAuthorityOperationResult>();

    auto transport = std::make_unique<SlowTransport>();
    SlowTransport *transportPointer = transport.get();
    ucm::ConfigurationSession session(std::move(transport));
    ucm::Configuration candidate = session.candidate();
    candidate.afe.digitalTgcAttenuationDb = 6;
    session.setCandidate(candidate);
    if (!session.prepare() || !session.validateCandidate()) {
        std::fprintf(stderr, "FAIL: candidate preparation failed\n");
        return 1;
    }

    QMutex sessionMutex;
    QThread workerThread;
    auto *worker = new DashboardPollWorker(&session, &sessionMutex);
    worker->moveToThread(&workerThread);
    QObject::connect(&workerThread, &QThread::finished,
                     worker, &QObject::deleteLater);

    QEventLoop completionLoop;
    ConfigurationApplyResult result;
    QObject::connect(worker, &DashboardPollWorker::configurationApplied,
                     &application,
                     [&](const ConfigurationApplyResult &completed) {
        result = completed;
        completionLoop.quit();
    });

    int heartbeatTicks = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout,
                     [&heartbeatTicks] { ++heartbeatTicks; });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout,
                     &completionLoop, &QEventLoop::quit);

    workerThread.start();
    heartbeat.start();
    watchdog.start(3000);
    QElapsedTimer elapsed;
    elapsed.start();
    QMetaObject::invokeMethod(
        worker, "applyPreparedConfiguration", Qt::QueuedConnection);
    completionLoop.exec();
    const qint64 elapsedMs = elapsed.elapsed();

    QEventLoop authorityLoop;
    ControlAuthorityOperationResult switchResult;
    ControlAuthorityOperationResult renewResult;
    int authorityCompletions = 0;
    QObject::connect(worker, &DashboardPollWorker::controlAuthorityCompleted,
                     &application,
                     [&](const ControlAuthorityOperationResult &completed) {
        ++authorityCompletions;
        if (authorityCompletions == 1) {
            switchResult = completed;
            QMetaObject::invokeMethod(worker, [worker] {
                worker->controlAuthority(
                    ControlAuthorityOperation::RenewLease, 2U);
            }, Qt::QueuedConnection);
        } else {
            renewResult = completed;
            authorityLoop.quit();
        }
    });
    const int heartbeatBeforeAuthority = heartbeatTicks;
    watchdog.start(3000);
    QMetaObject::invokeMethod(worker, [worker] {
        worker->controlAuthority(ControlAuthorityOperation::SwitchMode, 2U);
    }, Qt::QueuedConnection);
    authorityLoop.exec();
    const int authorityHeartbeatTicks = heartbeatTicks
        - heartbeatBeforeAuthority;

    const ConfigurationApplyResult successfulApply = result;
    ucm::Configuration rejectedCandidate = session.candidate();
    rejectedCandidate.afe.digitalTgcAttenuationDb = 12;
    session.setCandidate(rejectedCandidate);
    if (!session.prepare() || !session.validateCandidate()) {
        std::fprintf(stderr, "FAIL: rejection candidate preparation failed\n");
        workerThread.quit();
        workerThread.wait();
        return 1;
    }
    transportPointer->rejectNextReadback();
    result = {};
    watchdog.start(3000);
    QMetaObject::invokeMethod(
        worker, "applyPreparedConfiguration", Qt::QueuedConnection);
    completionLoop.exec();
    const ConfigurationApplyResult rejectedApply = result;

    QEventLoop pollLoop;
    QTimer pollWatchdog;
    pollWatchdog.setSingleShot(true);
    QObject::connect(&pollWatchdog, &QTimer::timeout,
                     &pollLoop, &QEventLoop::quit);
    DashboardPollResult pollResult;
    QObject::connect(worker, &DashboardPollWorker::completed,
                     &application,
                     [&](const DashboardPollResult &completed) {
        pollResult = completed;
        pollLoop.quit();
    });
    pollWatchdog.start(3000);
    QMetaObject::invokeMethod(worker, [worker] {
        worker->poll(false);
    }, Qt::QueuedConnection);
    pollLoop.exec();

    heartbeat.stop();
    workerThread.quit();
    workerThread.wait();

    bool passed = true;
    const auto expect = [&passed](bool condition, const char *message) {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        passed = false;
    };
    expect(successfulApply.committed, "background RAM commit must succeed");
    expect(successfulApply.confirmed, "independent readback must succeed");
    expect(successfulApply.failurePreservesActive,
           "confirmed transaction must report a known active object");
    expect(session.active().afe.digitalTgcAttenuationDb == 6,
           "confirmed configuration must become active");
    expect(elapsedMs >= 650,
           "test transport must exercise a visibly slow operation");
    expect(heartbeatTicks >= 20,
           "main event loop must remain responsive during apply/readback");
    expect(authorityCompletions == 2,
           "switch and renew must both complete asynchronously");
    expect(switchResult.result.success
               && switchResult.result.state.appliedMode == 2U
               && switchResult.result.state.phase == 7U,
           "manual-mode switch must reach host-active state");
    expect(renewResult.result.success
               && renewResult.result.state.hostLeaseDeadlineNs != 0U,
           "manual mode must renew the hardware lease");
    expect(authorityHeartbeatTicks >= 20,
           "main event loop must remain responsive during switch and renew");
    expect(rejectedApply.committed && !rejectedApply.confirmed
               && rejectedApply.failurePreservesActive,
           "explicit ARM rejection must remain directly retryable");
    expect(session.active().afe.digitalTgcAttenuationDb == 6,
           "explicit rejection must preserve the previous active object");
    expect(pollResult.runtime.success
               && pollResult.runtime.status.available,
           "poll must collect ARM runtime status independently");
    expect(pollResult.compoundRuntime.success
               && pollResult.compoundRuntime.snapshot.sourceBytesValidated
               && !pollResult.compoundRuntime.snapshot.runtimeChainClaim,
           "poll must carry CRS1 independently without inventing readiness");
    expect(!pollResult.telemetry.success
               && pollResult.telemetry.message.contains(
                   QStringLiteral("status=7")),
           "missing measurement snapshot must not erase runtime status");
    expect(transportPointer->runtimeReads() == 1
               && transportPointer->compositeRuntimeReads() == 1
               && transportPointer->telemetryReads() == 1,
           "each dashboard poll must issue one bounded read per status source");
    auto productTransport = std::make_unique<SlowTransport>();
    auto *productPointer = productTransport.get();
    productPointer->productMode = true;
    ucm::ConfigurationSession productSession(std::move(productTransport));
    QMutex productMutex;
    DashboardPollWorker productWorker(&productSession, &productMutex);
    DashboardPollResult productPoll;
    QObject::connect(&productWorker, &DashboardPollWorker::completed,
        [&](const DashboardPollResult &value) { productPoll = value; });
    productWorker.poll(true);
    productWorker.poll(true);
    expect(productPointer->readOrder == QStringList({"telemetry", "product", "runtime", "compound", "telemetry"}),
        "telemetry must precede configuration/runtime reads and each fast poll must retain telemetry");
    for (int tick = 0; tick < 5; ++tick) { QThread::msleep(20); productWorker.poll(false); }
    expect(productPointer->telemetryReads() == 7 && productPointer->runtimeReads() == 1
        && productPointer->compositeRuntimeReads() == 1
        && productPointer->runtimeProgressReads() == 1,
        "50Hz polls must not multiply runtime and composite traffic inside 500ms cache window");
    QThread::msleep(510);
    productWorker.poll(false);
    expect(productPointer->runtimeReads() == 2 && productPointer->compositeRuntimeReads() == 2,
        "runtime and composite caches must refresh after 500ms");
    expect(productPointer->runtimeProgressReads() == 2,
        "runtime progress must refresh independently after its 200ms cache window");
    expect(productPointer->productReads == 1,
        "product configuration must be cached across high frequency polls");
    expect(productPointer->waveformReads == 2,
        "revision 9 realtime waveform polls must use the implemented message-7 latest snapshot path");
    DashboardWaveformResult unsupportedCapture;
    QObject::connect(&productWorker,
        &DashboardPollWorker::waveformReadCompleted,
        [&](const DashboardWaveformResult &value) {
            unsupportedCapture = value;
        });
    constexpr quint64 kCurrentWaveformGeneration = 81U;
    productWorker.readWaveformViewport(6145U,
                                       kCurrentWaveformGeneration);
    expect(!unsupportedCapture.supported
            && unsupportedCapture.message.contains(QStringLiteral("6144")),
        "out-of-range waveform slices must be rejected before transport access");
    productWorker.readWaveformViewport(2048U,
                                       kCurrentWaveformGeneration);
    expect(!unsupportedCapture.supported
            && unsupportedCapture.message.contains(QStringLiteral("transport")),
        "missing waveform viewport capability must remain explicit");
    expect(productPoll.product.success,
        "product configuration must reach UI worker result");
    productWorker.disconnectTransport();
    expect(!productPoll.product.success && productPoll.product.deviceModel.isEmpty(),
        "disconnect must clear cached product configuration");
    productPointer->online = true;
    productWorker.poll(false);
    expect(productPointer->productReads == 2,
        "new connection must reread product state instead of reusing stale cache");
    productWorker.queryProductOperation();
    QCoreApplication::processEvents();
    expect(productPointer->productReads == 3,
        "configuration terminal confirmation must immediately refresh current/startup state despite 5s cache");
    QTemporaryDir exportDirectory;
    expect(exportDirectory.isValid(), "temporary export directory must exist");
    QEventLoop exportLoop;
    QTimer exportWatchdog;
    exportWatchdog.setSingleShot(true);
    QObject::connect(&exportWatchdog, &QTimer::timeout, &exportLoop, &QEventLoop::quit);
    ucm::DiagnosticPackageResult packageResult;
    QObject::connect(&productWorker, &DashboardPollWorker::productPackageCompleted,
        [&](const ucm::DiagnosticPackageResult &value) { packageResult = value; exportLoop.quit(); });
    int pollsDuringExport = 0;
    bool disconnectDuringExport = false;
    QObject::connect(&productWorker, &DashboardPollWorker::productPackageProgress,
        [&](const QString &) {
            ++pollsDuringExport;
            if (disconnectDuringExport) productWorker.disconnectTransport();
            else productWorker.poll(false);
        });
    exportWatchdog.start(3000);
    productWorker.exportProductPackage(exportDirectory.path(), QByteArray("row_kind,force_N\n"));
    exportLoop.exec();
    expect(packageResult.success && productPointer->logChunks == 2,
        "product export must assemble multiple validated chunks");
    expect(pollsDuringExport >= 2,
        "measurement polling must be serviceable between export chunks");
    QFile exportedLog(QDir(packageResult.packagePath).filePath("device-log-1.bin"));
    expect(exportedLog.open(QIODevice::ReadOnly) && exportedLog.readAll() == QByteArray(70040, 'A'),
        "product package must preserve exact raw log bytes");
    exportedLog.close();
    expect(QFile::exists(QDir(packageResult.packagePath).filePath("SHA256SUMS.txt")),
        "product package must include hashes");
    expect(QFile::exists(QDir(packageResult.packagePath).filePath("software-identity.json")),
        "product package identifies exporting software");
    productPointer->conflictLog = true;
    packageResult = {};
    exportWatchdog.start(3000);
    productWorker.exportProductPackage(exportDirectory.path(), QByteArray("row_kind,force_N\n"));
    exportLoop.exec();
    expect(!packageResult.success && !packageResult.packagePath.isEmpty(),
        "identity conflict must produce explicitly incomplete evidence");
    expect(!QFile::exists(QDir(packageResult.packagePath).filePath("device-log-1.bin"))
        && QFile::exists(QDir(packageResult.packagePath).filePath("READS_INCOMPLETE")),
        "identity conflict must not export partial log bytes");
    productPointer->conflictLog = false;
    disconnectDuringExport = true;
    packageResult = {};
    exportWatchdog.start(3000);
    productWorker.exportProductPackage(exportDirectory.path(), QByteArray("row_kind,force_N\n"));
    exportLoop.exec();
    expect(!packageResult.success && packageResult.packagePath.isEmpty(),
        "disconnect must cancel export before writing unvalidated files");
    ucm::ProductOperationResult blockedOperation;
    QObject::connect(&productWorker, &DashboardPollWorker::productOperationUpdated,
        [&](const ucm::ProductOperationResult &value, bool) { blockedOperation = value; });
    productPointer->online = true;
    productWorker.submitProductDocument(0, 1, QByteArray("{}"), true);
    expect(!blockedOperation.ok && blockedOperation.outcome == QStringLiteral("failed"),
        "uncommissioned backend must reject authorized product writes before submission");
    const int beforeInvalidReads = productPointer->productReads;
    productWorker.submitProductDocument(2, 1, QByteArray("{}"), true);
    productWorker.submitProductDocument(0, 4, QByteArray("{}"), true);
    expect(productPointer->productReads == beforeInvalidReads && !blockedOperation.ok,
        "invalid domain/operation must be rejected before USB reads");
    productPointer->commissioned = true;
    const int beforeStaleWrites = productPointer->productWrites;
    const QString staleIdentity = QStringLiteral("{\"identitySha256\":\"")
        + QString(64, QLatin1Char('b')) + QStringLiteral("\"}");
    productWorker.submitProductDocument(0, 3, QByteArray("{}"), true, staleIdentity);
    expect(!blockedOperation.ok && blockedOperation.outcome == QStringLiteral("failed")
            && blockedOperation.message.contains(QStringLiteral("身份已改变"))
            && productPointer->productWrites == beforeStaleWrites,
        "stale calibration identity must be rejected before sending USB configuration");
    productPointer->commissioned = false;
    productWorker.stageProductUpgrade(QStringLiteral("missing.bin"), 1, QStringLiteral("model"), QStringLiteral("build"), false);
    productWorker.activateProductUpgrade(false);
    expect(productPointer->upgradeWrites == 0 && productPointer->upgradeQueries == 0,
        "unauthorized upgrade stage and activation must send zero requests");
    productWorker.activateProductUpgrade(true);
    expect(blockedOperation.outcome == QStringLiteral("unresolved") && productPointer->upgradeWrites == 1,
        "lost activation receipt must preserve unresolved operation");
    productPointer->recoverUpgrade = true;
    productWorker.disconnectTransport();
    productPointer->online = true;
    productWorker.poll(false);
    QEventLoop recoveryLoop;
    QTimer recoveryDeadline;
    recoveryDeadline.setSingleShot(true);
    QObject::connect(&recoveryDeadline, &QTimer::timeout, &recoveryLoop, &QEventLoop::quit);
    QObject::connect(&productWorker, &DashboardPollWorker::productOperationUpdated,
        &recoveryLoop, [&](const ucm::ProductOperationResult &value, bool) {
            if (value.outcome == QStringLiteral("succeeded")) recoveryLoop.quit();
        });
    recoveryDeadline.start(2000);
    recoveryLoop.exec();
    expect(productPointer->upgradeQueries == 2 && productPointer->upgradeWrites == 1
        && blockedOperation.outcome == QStringLiteral("succeeded"),
        "reconnect must query activating state through completion without resending activation");
    return passed ? 0 : 1;
}
