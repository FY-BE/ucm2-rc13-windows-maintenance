#include "readonly_probe.h"
#include "arm_force_csv.h"
#include "poll_schedule.h"
#include "product_log_download.h"
#include "usb_functionfs_transport.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>
#include <QThread>
#include <QtEndian>
#include <algorithm>

namespace {
bool save(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool json(const QString &path, const QJsonObject &value) { return save(path, QJsonDocument(value).toJson()); }
QJsonObject agcPointJson(const ucm::UsbAgcPointV9 &point) {
    QJsonArray lna;
    for (const auto value : point.lnaDb) lna.append(qint64(value));
    return {{"lna_db", lna}, {"pga_db", qint64(point.pgaDb)},
        {"vcntl_code", qint64(point.vcntlCode)},
        {"nominal_hv_v", qint64(point.nominalHvV)},
        {"burst_cycles", qint64(point.burstCycles)}};
}
QJsonObject runtimeProgressJson(const ucm::UsbRuntimeProgressResultV9 &result) {
    const auto &p = result.progress;
    return {{"success", result.success}, {"message", result.message},
        {"generation", QString::number(p.generation)},
        {"updated_monotonic_ns", QString::number(p.updatedMonotonicNs)},
        {"stage", qint64(p.stage)}, {"stage_text", ucm::usbRuntimeStageTextV9(p.stage)},
        {"stage_item", qint64(p.stageItem)}, {"stage_item_count", qint64(p.stageItemCount)},
        {"overall_permille", qint64(p.overallPermille)},
        {"stage_elapsed_ms", QString::number(p.stageElapsedMs)},
        {"stage_limit_ms", QString::number(p.stageLimitMs)},
        {"estimated_remaining_ms", QString::number(p.estimatedRemainingMs)},
        {"wait_reason", qint64(p.waitReason)}, {"can_measure", p.canMeasure},
        {"quality", qint64(p.qualityLevel)},
        {"quality_text", ucm::usbRuntimeQualityTextV9(p.qualityLevel)},
        {"plc_state", qint64(p.plcState)}, {"plc_fresh", p.plcFresh},
        {"template_valid_mask", qint64(p.templateValidMask)},
        {"tare_state", qint64(p.tareState)}, {"tare_generation", qint64(p.tareGeneration)},
        {"last_action", qint64(p.lastAction)}, {"last_action_result", qint64(p.lastActionResult)},
        {"last_action_transaction_id", QString::number(p.lastActionTransactionId)},
        {"fault_code", qint64(p.faultCode)},
        {"active_device_model_id", qint64(p.activeDeviceModelId)},
        {"startup_device_model_id", qint64(p.startupDeviceModelId)},
        {"pending_device_model_id", qint64(p.pendingDeviceModelId)},
        {"model_switch_state", qint64(p.modelSwitchState)},
        {"model_switch_result", qint64(p.modelSwitchResult)},
        {"model_profile_origin", qint64(p.modelProfileOrigin)},
        {"model_switch_transaction_id", QString::number(p.modelSwitchTransactionId)},
        {"current", agcPointJson(p.current)},
        {"best", agcPointJson(p.best)}};
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("UcmReadOnlyProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription("Read-only W2 evidence recorder. No hardware is opened without --usb-read-only. Physical use requires prior target-specific authorization.");
    parser.addHelpOption();
    parser.addOption({"simulate", "Exercise the recorder/output only; no USB. Does not simulate the full ARM receiver."});
    parser.addOption({"usb-read-only", "Open the unique matching USB device. Only after explicit physical authorization."});
    parser.addOption({"target-label", "Operator-confirmed physical target label (not a verified USB serial).", "label"});
    parser.addOption({"output", "New evidence directory (must not exist).", "directory"});
    parser.addOption({"duration-seconds", "Physical run length, 1..86400 seconds.", "seconds", "1800"});
    parser.addOption({"logs", "Read complete logs repeatedly in bounded chunks, interleaved with measurements."});
    parser.addOption({"waveform-once", "Save one verified 16448-byte ARM waveform object (physical read-only mode only)."});
    parser.addOption({"log-budget-mib", "Stop log downloads after this local byte budget; 16..4096 MiB.", "mib", "256"});
    parser.process(app);
    const bool simulation = parser.isSet("simulate"), physical = parser.isSet("usb-read-only");
    bool durationOk = false;
    const int duration = parser.value("duration-seconds").toInt(&durationOk);
    bool budgetOk = false;
    const int logBudgetMiB = parser.value("log-budget-mib").toInt(&budgetOk);
    const quint64 logBudget = quint64(logBudgetMiB) * 1024 * 1024;
    if (simulation == physical || !parser.isSet("output") || !durationOk || duration < 1 || duration > 86400
        || !budgetOk || logBudgetMiB < 16 || logBudgetMiB > 4096
        || (physical && parser.value("target-label").trimmed().isEmpty())
        || (simulation && parser.isSet("waveform-once"))) {
        QTextStream(stderr) << "Choose exactly --simulate or --usb-read-only; supply a NEW --output directory. Physical mode also requires --target-label. No USB opened.\n";
        return 2;
    }
    const QFileInfo destination(QDir::cleanPath(QFileInfo(parser.value("output")).absoluteFilePath()));
    if (!QDir().mkpath(destination.absolutePath()) || !QDir(destination.absolutePath()).mkdir(destination.fileName())) {
        QTextStream(stderr) << "Output must be a new directory. No USB opened.\n"; return 2;
    }
    const QDir out(destination.absoluteFilePath());
    if (!save(out.filePath("INCOMPLETE"), "Run has not reached its final report. No acceptance claim.\n")) return 3;
    QFile csv(out.filePath("measurements.csv")), events(out.filePath("events.jsonl")), wire(out.filePath("requests.jsonl"));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !events.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || !wire.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 3;
    bool ioOk = csv.write(ArmForceCsv::header()) > 0;
    auto event = [&](QJsonObject value) {
        value["observed_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
        ioOk = events.write(bytes) == bytes.size() && ioOk;
    };
    ucm::probe::Statistics stats;
    QJsonObject transportEvidence;
    QJsonObject waveformEvidence {{"requested", parser.isSet("waveform-once")}, {"captured", false}};
    QJsonObject logCapabilityEvidence {{"requested", parser.isSet("logs")}, {"active", false}, {"observed", false}};
    QJsonObject progressEvidence {{"observed", false}, {"successful_reads", 0}, {"failed_reads", 0}};
    QElapsedTimer elapsed; elapsed.start();
    if (simulation) {
        event({{"type", "recorder_simulation"}, {"hardware_accessed", false}, {"full_receiver_simulation", false}});
        for (int i = 0; i < 25; ++i) {
            stats.connection(i != 10 && i != 11);
            ucm::TelemetrySnapshot s;
            s.productResult = true; s.diagnosticFieldsAvailable = false;
            s.success = i != 10 && i != 11;
            s.formalForceValid = s.success && (i < 5 || i > 7);
            s.primaryReasonCode = s.success && !s.formalForceValid ? 9 : 0;
            s.generation = 9007199254740993ULL; s.sessionId = 1; s.sequence = i + 1;
            s.frameCounter = i + 1; s.captureRequestId = i + 1; s.publishedMonotonicNs = 100000 + i;
            s.message = QStringLiteral("SIMULATED recorder observation");
            s.formalTotalN = 400; s.forceAvailableMask = s.formalForceValid ? 15 : 0;
            for (auto &rod : s.rod) rod.forceN = 100;
            stats.sample(s, i * 100, i == 8 ? 260 : 20);
            const auto row = s.success ? ArmForceCsv::measurement(s, i * 100) : ArmForceCsv::gap(s.message, i * 100);
            ioOk = csv.write(row) == row.size() && ioOk;
        }
        transportEvidence = {{"hardware_accessed", false}, {"readonly_wire_guard_tested_separately", true}};
    } else {
        auto guard = std::make_unique<ucm::probe::ReadOnlyBackend>(std::make_unique<ucm::LibusbBackend>());
        auto *guardView = guard.get();
        guard->trace = [&](QJsonObject value) {
            value["host_elapsed_ms"] = elapsed.elapsed();
            value["kind"] = "request_attempt_not_bus_capture";
            const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
            ioOk = wire.write(bytes) == bytes.size() && ioOk;
        };
        ucm::UsbFunctionfsTransport transport(std::move(guard), false);
        qint64 nextConnect = 0, nextConfig = 0, nextCatalog = 0, nextProgress = 0;
        int retry = 0, sourceIndex = 0, captureIndex = 0;
        bool logBudgetExhausted = false;
        bool waveformAttempted = false;
        ucm::ProductLogDownload download;
        QVector<ucm::DeviceLogSource> sources;
        while (elapsed.elapsed() < qint64(duration) * 1000 && ioOk) {
            const qint64 loopStart = elapsed.elapsed();
            if (!transport.info().realUsbOpened && loopStart >= nextConnect) {
                ++stats.reconnectAttempts;
                const auto connected = transport.reconnect();
                event({{"type", "connect_attempt"}, {"success", connected.success}, {"message", connected.message}});
                nextConnect = elapsed.elapsed() + ucm::ui::connectionRetryDelayMs(retry++);
                if (connected.success) { retry = 0; nextConfig = 0; nextCatalog = 0; }
            }
            const bool connected = transport.info().realUsbOpened;
            stats.connection(connected);
            if (connected) {
                const auto discovery = transport.usbExtendedDiscovery();
                const bool logCatalogActive = discovery.available
                    && (discovery.capabilities.activeFeatureMask & ucm::kUsbExtendedFeatureLogCatalogV2) != 0U;
                const bool runtimeProgressActive = discovery.available
                    && (discovery.capabilities.activeFeatureMask & ucm::kUsbExtendedFeatureRuntimeControlV1) != 0U;
                if (parser.isSet("logs") && !logCapabilityEvidence["observed"].toBool()) {
                    logCapabilityEvidence["observed"] = true;
                    logCapabilityEvidence["active"] = logCatalogActive;
                    logCapabilityEvidence["active_feature_mask"] = QStringLiteral("0x%1")
                        .arg(discovery.capabilities.activeFeatureMask, 0, 16);
                    event({{"type", "log_capability"}, {"active", logCatalogActive},
                        {"active_feature_mask", logCapabilityEvidence["active_feature_mask"]}});
                }
                QElapsedTimer request; request.start();
                const auto s = transport.readTelemetrySnapshot();
                stats.sample(s, elapsed.elapsed(), request.elapsed());
                const auto row = s.success ? ArmForceCsv::measurement(s, QDateTime::currentMSecsSinceEpoch())
                    : ArmForceCsv::gap(s.message, QDateTime::currentMSecsSinceEpoch());
                ioOk = csv.write(row) == row.size() && ioOk;
                if (!s.success) event({{"type", "unavailable"}, {"message", s.message}, {"arm_reason", qint64(s.primaryReasonCode)}});
                if (runtimeProgressActive && transport.info().realUsbOpened
                    && elapsed.elapsed() >= nextProgress) {
                    const auto progress = transport.readRuntimeProgressV9();
                    const int successes = progressEvidence["successful_reads"].toInt()
                        + (progress.success ? 1 : 0);
                    const int failures = progressEvidence["failed_reads"].toInt()
                        + (progress.success ? 0 : 1);
                    progressEvidence = runtimeProgressJson(progress);
                    progressEvidence["observed"] = true;
                    progressEvidence["active_feature"] = true;
                    progressEvidence["successful_reads"] = successes;
                    progressEvidence["failed_reads"] = failures;
                    if (progress.success)
                        ioOk = json(out.filePath("latest-runtime-progress.json"), progressEvidence) && ioOk;
                    else
                        event({{"type", "runtime_progress_failure"}, {"message", progress.message}});
                    if (progress.success) {
                        QJsonObject progressEvent = progressEvidence;
                        progressEvent["type"] = "runtime_progress";
                        event(progressEvent);
                    }
                    nextProgress = elapsed.elapsed() + 200;
                }
                if (parser.isSet("waveform-once") && !waveformAttempted && s.success) {
                    waveformAttempted = true;
                    const auto waveform = transport.readWaveformObject();
                    const bool valid = waveform.success && waveform.data.size() == 16448;
                    const bool written = valid && save(out.filePath("waveform.bin"), waveform.data);
                    waveformEvidence["captured"] = written;
                    waveformEvidence["bytes"] = waveform.data.size();
                    waveformEvidence["message"] = waveform.message;
                    if (written) {
                        waveformEvidence["sha256"] = QString::fromLatin1(
                            QCryptographicHash::hash(waveform.data, QCryptographicHash::Sha256).toHex());
                        const auto *header = reinterpret_cast<const uchar *>(waveform.data.constData());
                        waveformEvidence["frame_counter"] = QString::number(qFromLittleEndian<quint64>(header + 24));
                        waveformEvidence["sample_rate_hz"] = qint64(qFromLittleEndian<quint32>(header + 32));
                        waveformEvidence["samples_per_channel"] = qint64(qFromLittleEndian<quint32>(header + 40));
                        waveformEvidence["channels"] = qint64(qFromLittleEndian<quint32>(header + 44));
                    }
                    event({{"type", "waveform_once"}, {"captured", written},
                        {"bytes", waveform.data.size()}, {"message", waveform.message}});
                }
                if (transport.info().realUsbOpened && elapsed.elapsed() >= nextConfig) {
                    const auto config = transport.readProductState();
                    if (config.supported && !config.success) ++stats.configFailures;
                    ioOk = json(out.filePath("latest-product-state.json"), {{"supported", config.supported},
                        {"success", config.success}, {"message", config.message},
                        {"device_model", config.deviceModel}, {"input_policy", config.inputPolicy}}) && ioOk;
                    nextConfig = elapsed.elapsed() + 5000;
                }
                if (parser.isSet("logs") && logCatalogActive && !logBudgetExhausted && transport.info().realUsbOpened) {
                    if (!download.active() && sourceIndex >= sources.size() && elapsed.elapsed() >= nextCatalog) {
                        const auto catalog = transport.readLogSources();
                        sources = catalog.sources; sourceIndex = 0; nextCatalog = elapsed.elapsed() + 30000;
                        event({{"type", "catalog"}, {"success", catalog.success}, {"message", catalog.message}, {"entries", sources.size()}});
                        if (!catalog.success) ++stats.logFailures;
                    }
                    if (!download.active() && sourceIndex < sources.size()) {
                        if (sources[sourceIndex].totalBytes > logBudget - std::min(logBudget, stats.logBytes)) {
                            logBudgetExhausted = true;
                            event({{"type", "log_budget_exhausted"}, {"budget_bytes", QString::number(logBudget)}});
                        } else if (!download.begin(sources[sourceIndex])) {
                            ++stats.logFailures; event({{"type", "log_rejected"}, {"message", download.error()}}); ++sourceIndex;
                        }
                    }
                    if (download.active()) {
                        const auto source = download.source();
                        const auto chunk = transport.readLogChunk(source.sourceId, download.offset(), download.nextBytes(), source.snapshotId);
                        if (!download.accept(chunk)) {
                            ++stats.logFailures; event({{"type", "log_conflict_or_failure"}, {"message", download.error()}});
                            download.cancel("discarded"); sources.clear(); sourceIndex = 0; nextCatalog = 0;
                        } else if (download.complete()) {
                            const QString name = QStringLiteral("log-%1.bin").arg(++captureIndex);
                            ioOk = save(out.filePath(name), download.data()) && ioOk;
                            ++stats.logFiles; stats.logBytes += source.totalBytes;
                            event({{"type", "log_complete"}, {"file", name}, {"device_name", source.name},
                                {"mutable_file", source.mutableFile}, {"snapshot_id", QString::number(source.snapshotId)},
                                {"local_sha256", QString::fromLatin1(download.localSha256().toHex())}, {"device_expected_sha_verified", false}});
                            download.cancel("complete"); ++sourceIndex;
                        }
                    }
                }
            }
            if (!transport.info().realUsbOpened && connected) {
                stats.connection(false); download.cancel("disconnected"); sources.clear(); sourceIndex = 0;
                nextConnect = elapsed.elapsed() + ucm::ui::connectionRetryDelayMs(0); retry = 1;
                event({{"type", "disconnected"}});
            }
            ioOk = csv.flush() && events.flush() && wire.flush() && ioOk;
            const auto sleep = 100 - (elapsed.elapsed() - loopStart);
            if (sleep > 0) QThread::msleep(static_cast<unsigned long>(sleep));
        }
        if (download.active()) event({{"type", "partial_log_discarded_at_stop"}});
        transportEvidence = transport.evidence();
        transportEvidence["readonly_guard_denied_requests"] = QString::number(guardView->deniedRequests);
        transport.disconnect();
    }
    ioOk = csv.flush() && events.flush() && wire.flush() && ioOk;
    csv.close(); events.close(); wire.close();
    auto report = stats.report(simulation, elapsed.elapsed());
    report["operator_target_label"] = parser.value("target-label");
    report["log_budget_bytes"] = QString::number(logBudget);
    report["log_catalog_capability"] = logCapabilityEvidence;
    report["target_label_is_verified_usb_identity"] = false;
    report["transport"] = transportEvidence;
    report["runtime_progress"] = progressEvidence;
    report["waveform_once"] = waveformEvidence;
    QFile executable(QCoreApplication::applicationFilePath()); QCryptographicHash executableHash(QCryptographicHash::Sha256);
    const bool identityOk = executable.open(QIODevice::ReadOnly) && executableHash.addData(&executable);
    report["probe_executable_sha256"] = identityOk ? QString::fromLatin1(executableHash.result().toHex()) : QString();
    report["capture_finished"] = ioOk;
    if (!identityOk || !ioOk || !json(out.filePath("report.json"), report)) return 3;
    QByteArray sums;
    for (const auto &fileName : out.entryList(QDir::Files, QDir::Name)) {
        if (fileName == "INCOMPLETE") continue;
        QFile file(out.filePath(fileName)); QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return 3;
        sums += hash.result().toHex() + " *" + fileName.toUtf8() + '\n';
    }
    if (!save(out.filePath("SHA256SUMS.txt"), sums) || !QFile::remove(out.filePath("INCOMPLETE"))) return 3;
    QTextStream(stdout) << out.absolutePath() << "\nRecorded observations; NOT automatic hardware acceptance.\n";
    return !simulation && (!stats.paired || stats.unavailable || stats.logFailures || stats.configFailures
        || (parser.isSet("waveform-once") && !waveformEvidence["captured"].toBool())) ? 4 : 0;
}
