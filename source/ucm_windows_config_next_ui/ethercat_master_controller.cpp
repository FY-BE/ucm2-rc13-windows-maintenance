#include "ethercat_master_controller.h"
#include "fqx_machine_models.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
quint64 value64(const QVariantMap &settings, const char *name)
{
    bool ok = false;
    const quint64 value = settings.value(QLatin1String(name)).toString().toULongLong(&ok);
    return ok ? value : settings.value(QLatin1String(name)).toULongLong();
}

QString stateName(int status)
{
    QString state;
    switch (status & 0x0f) {
    case 1: state = QStringLiteral("INIT"); break;
    case 2: state = QStringLiteral("PREOP"); break;
    case 4: state = QStringLiteral("SAFEOP"); break;
    case 8: state = QStringLiteral("OP"); break;
    default: state = QStringLiteral("未知"); break;
    }
    if (status & 0x10) state += QStringLiteral(" + ERROR");
    return state;
}

bool machineModelValid(quint64 model)
{
    return model <= 0xffffU
        && ucm::ethercat::isKnownFqxMachineModel(static_cast<std::uint16_t>(model));
}
}

EthercatMasterController::EthercatMasterController(bool enabled, QObject *parent)
    : QObject(parent), m_enabled(enabled)
{
    m_statusText = enabled ? QStringLiteral("尚未启动模拟主站")
                           : QStringLiteral("离线预览不启动真实 EtherCAT 主站");
}

EthercatMasterController::~EthercatMasterController()
{
    m_stopRequested.store(true);
    if (m_thread.joinable()) m_thread.join();
}

QVariantList EthercatMasterController::machineModels() const
{
    QVariantList models;
    models.reserve(static_cast<qsizetype>(ucm::ethercat::kFqxMachineModels.size()));
    for (const auto &model : ucm::ethercat::kFqxMachineModels) {
        models.append(QVariantMap {
            {QStringLiteral("label"), QStringLiteral("%1  ·  0x%2")
                 .arg(QString::fromLatin1(model.name))
                 .arg(QString::number(model.code, 16)
                          .rightJustified(4, QLatin1Char('0')).toUpper())},
            {QStringLiteral("value"), model.code},
        });
    }
    return models;
}

void EthercatMasterController::joinFinishedThread()
{
    if (m_thread.joinable() && !m_busy) m_thread.join();
}

void EthercatMasterController::start(const QString &adapterId, const QString &mac,
                                     const QVariantMap &settings)
{
    if (!m_enabled || m_busy || adapterId.isEmpty()) return;
    joinFinishedThread();
    m_stopRequested.store(false);
    m_busy = true;
    m_running = false;
    m_statusText = QStringLiteral("正在配置 EtherCAT 从站…");
    m_profile.clear();
    m_alState = QStringLiteral("INIT");
    m_alStatusCode = 0;
    m_workingCounter = 0;
    m_completedCycles = 0;
    m_droppedCycles = 0;
    m_input = {};
    m_evidence.clear();
    m_evidencePath.clear();
    emit changed();

    ucm::ethercat::MasterRunOptions options;
    options.adapterId = adapterId;
    QString macHex = mac;
    macHex.remove(QLatin1Char(':'));
    macHex.remove(QLatin1Char('-'));
    options.sourceMac = QByteArray::fromHex(macHex.toLatin1());
    options.cycles = 0;
    options.periodMs = qBound(1, settings.value(QStringLiteral("periodMs"), 1).toInt(), 1000);
    const quint64 machineModel = value64(settings, "machineModel");
    const quint64 forceSetpoint = value64(settings, "clampingForceSetpointKn");
    const quint64 moldThicknessMm = value64(settings, "moldThickness");
    const quint64 moldThicknessRaw = moldThicknessMm * 10U;
    const quint64 machineState = value64(settings, "machineState");
    if (!machineModelValid(machineModel) || forceSetpoint > 180000U
        || moldThicknessMm == 0 || moldThicknessRaw >= 0xffffU
        || machineState > 4U) {
        m_busy = false;
        m_statusText = QStringLiteral(
            "FQX 输出参数无效：请核对机器型号、锁模力设定值、模厚和机器状态");
        emit changed();
        return;
    }
    options.output.machineModel = static_cast<quint16>(machineModel);
    options.output.currentUtcMs = value64(settings, "currentUtcMs");
    options.output.clampingForceSetpointKn = static_cast<quint32>(forceSetpoint);
    /* FQX V1.0 carries mold thickness as a uint16 in 0.1 mm units.  The UI
     * remains in physical millimetres. */
    options.output.moldThickness = static_cast<quint16>(moldThicknessRaw);
    options.output.machineState = static_cast<quint8>(machineState);
    options.output.deviceEnabledUtcMs = value64(settings, "deviceEnabledUtcMs");
    options.output.accumulatedRuntimeMs = value64(settings, "accumulatedRuntimeMs");
    options.stopRequested = &m_stopRequested;
    options.liveUpdate = [this](int alStatus, int alCode, int wkc, int cycles,
                                int inputBytes,
                                const ucm::ethercat::SlaveInput &input) {
        QMetaObject::invokeMethod(this, [this, alStatus, alCode, wkc, cycles,
                                         inputBytes, input] {
            applyLive(alStatus, alCode, wkc, cycles, inputBytes, input);
        }, Qt::QueuedConnection);
    };
    m_thread = std::thread([this, options] {
        const auto result = ucm::ethercat::MasterSession::run(options);
        QMetaObject::invokeMethod(this, [this, result] { applyResult(result); },
                                  Qt::QueuedConnection);
    });
}

void EthercatMasterController::stop()
{
    if (!m_busy) return;
    m_statusText = QStringLiteral("正在停止并返回 INIT…");
    m_stopRequested.store(true);
    emit changed();
}

void EthercatMasterController::applyLive(
    int alStatus, int alCode, int wkc, int cycles, int inputBytes,
    const ucm::ethercat::SlaveInput &input)
{
    m_running = (alStatus & 0x0f) == 8;
    m_alState = stateName(alStatus);
    m_alStatusCode = alCode;
    m_workingCounter = wkc;
    m_completedCycles = cycles;
    m_input = input;
    Q_UNUSED(inputBytes)
    m_profile = QStringLiteral("FQX V1.0 LE · 33/35 · DC-SYNC0");
    m_statusText = m_running ? QStringLiteral("OP 周期通信中")
                             : QStringLiteral("正在从 SAFEOP 切换到 OP…");
    emit changed();
}

void EthercatMasterController::applyResult(
    const ucm::ethercat::MasterRunResult &result)
{
    m_busy = false;
    m_running = false;
    m_statusText = result.status + (result.returnedToInit
        ? QStringLiteral(" · 已返回 INIT") : QStringLiteral(" · INIT 回退失败"));
    m_profile = result.profile;
    m_alState = result.returnedToInit ? QStringLiteral("INIT")
                                      : stateName(result.alStatus);
    m_alStatusCode = result.alStatusCode;
    m_workingCounter = result.maximumWkc;
    m_completedCycles = result.completedCycles;
    m_droppedCycles = result.droppedCycles;
    m_input = result.lastInput;
    m_evidence = result.evidence.toVariantMap();
    const QString directory = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("ethercat-sessions"));
    if (QDir().mkpath(directory)) {
        const QString name = QStringLiteral("ethercat-%1-%2.json")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                 QUuid::createUuid().toString(QUuid::Id128).left(8));
        const QString path = QDir(directory).filePath(name);
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(result.evidence).toJson(QJsonDocument::Indented));
            if (file.commit()) m_evidencePath = QDir::toNativeSeparators(path);
        }
    }
    if (m_evidencePath.isEmpty())
        m_statusText += QStringLiteral(" · 证据文件保存失败");
    emit changed();
}
