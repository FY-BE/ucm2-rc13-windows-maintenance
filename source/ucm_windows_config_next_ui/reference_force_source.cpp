#include "reference_force_source.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cmath>

namespace {

constexpr double minimumAcceptedConfidence = 0.80;
constexpr qint64 staleAfterMs = 2500;
constexpr qint64 maximumTimestampSkewMs = 10000;

bool finiteNumber(const QJsonValue &value, double *number)
{
    if (!value.isDouble()) return false;
    const double candidate = value.toDouble();
    if (!std::isfinite(candidate)) return false;
    *number = candidate;
    return true;
}

QString statusMessageFromStderr(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error == QJsonParseError::NoError
            && document.isObject()) {
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("schema")).toString()
                == QStringLiteral("ucm-reference-force-status/v1")) {
            return object.value(QStringLiteral("message")).toString();
        }
    }
    return QString::fromUtf8(line).trimmed();
}

} // namespace

bool decodeReferenceForcePayload(const QByteArray &line,
                                 ReferenceForceFrame *frame,
                                 QString *error)
{
    if (frame == nullptr) {
        if (error != nullptr) *error = QStringLiteral("输出对象为空。");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = QStringLiteral("标准力不是有效JSON对象。");
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema_version")).toString()
            != QStringLiteral("forceInput_v1")
            || object.value(QStringLiteral("source")).toString()
                != QStringLiteral("force_input_camera")
            || object.value(QStringLiteral("evidence_level")).toString()
                != QStringLiteral("force_input_camera")
            || object.value(QStringLiteral("status")).toString()
                != QStringLiteral("ok")) {
        if (error != nullptr) *error = QStringLiteral("标准力身份或状态不符合合同。");
        return false;
    }

    double timestamp = 0.0;
    double confidence = 0.0;
    if (!finiteNumber(object.value(QStringLiteral("timestamp_ms")), &timestamp)
            || timestamp <= 0.0
            || !finiteNumber(object.value(QStringLiteral("confidence")),
                             &confidence)
            || confidence < minimumAcceptedConfidence || confidence > 1.0) {
        if (error != nullptr) *error = QStringLiteral("标准力时间戳或置信度无效。");
        return false;
    }

    const QJsonValue forceValue = object.value(QStringLiteral("force_kN"));
    if (!forceValue.isArray() || forceValue.toArray().size() != 4) {
        if (error != nullptr) *error = QStringLiteral("标准力必须正好包含四根杆。");
        return false;
    }
    ReferenceForceFrame decoded;
    decoded.timestampMs = qRound64(timestamp);
    decoded.confidence = confidence;
    const QJsonArray forces = forceValue.toArray();
    for (int rod = 0; rod < 4; ++rod) {
        double force = 0.0;
        if (!finiteNumber(forces.at(rod), &force) || force < 0.0) {
            if (error != nullptr) {
                *error = QStringLiteral("标准力杆%1数值无效。").arg(rod + 1);
            }
            return false;
        }
        decoded.forceKn[rod] = force;
    }
    *frame = decoded;
    return true;
}

ReferenceForceSource::ReferenceForceSource(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &ReferenceForceSource::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &ReferenceForceSource::readStandardError);
    connect(&m_process, &QProcess::started, this, [this] {
        setStatus(QStringLiteral("相机桥已启动，等待有效标准力"), false);
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
        if (!m_userStopping) {
            setStatus(QStringLiteral("标准力相机桥启动失败：%1")
                          .arg(m_process.errorString()), false);
        }
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
        if (m_userStopping) {
            setStatus(QStringLiteral("标准力相机已断开"), false);
        } else {
            setStatus(QStringLiteral("标准力相机桥已退出（%1）")
                          .arg(exitCode), false);
        }
    });
    m_staleTimer.setInterval(500);
    connect(&m_staleTimer, &QTimer::timeout,
            this, &ReferenceForceSource::updateStaleness);
    m_staleTimer.start();
}

ReferenceForceSource::~ReferenceForceSource()
{
    stop();
}

bool ReferenceForceSource::running() const
{
    return m_process.state() != QProcess::NotRunning;
}

QString ReferenceForceSource::helperInvocation(QStringList *prefixArguments)
{
    if (prefixArguments) prefixArguments->clear();
    QString helper = qEnvironmentVariable("UCM_REFERENCE_FORCE_HELPER");
    if (helper.isEmpty()) {
        const QString root = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("reference_force_camera"));
        const QString executable = QDir(root).filePath(
            QStringLiteral("UcmReferenceForceBridge.exe"));
        const QString script = QDir(root).filePath(
            QStringLiteral("reference_force_bridge.py"));
        helper = QFileInfo::exists(executable) ? executable : script;
    }
    if (!QFileInfo::exists(helper)) return {};

    QString program = helper;
    if (QFileInfo(helper).suffix().compare(QStringLiteral("py"),
                                           Qt::CaseInsensitive) == 0) {
        program = qEnvironmentVariable("UCM_REFERENCE_FORCE_PYTHON");
        if (program.isEmpty()) {
            program = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
        }
        if (program.isEmpty()) {
            program = QStandardPaths::findExecutable(QStringLiteral("python"));
        }
        if (program.isEmpty()) return {};
        if (prefixArguments) *prefixArguments << QStringLiteral("-u") << helper;
    }
    return program;
}

QStringList ReferenceForceSource::cameraSdkArguments()
{
    QStringList arguments;
    // Pass the root explicitly: the bridge needs both the wrapper and runtime,
    // and the vendor WinDLL loader cannot rely on PATH alone.
    const QString bundledRoot = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("MVS"));
    if (QFileInfo::exists(QDir(bundledRoot).filePath(
            QStringLiteral("Development/Samples/Python/MvImport/MvCameraControl_class.py")))
        && QFileInfo::exists(QDir(bundledRoot).filePath(
            QStringLiteral("Runtime/Win64_x64/MvCameraControl.dll")))) {
        arguments << QStringLiteral("--sdk-dir") << bundledRoot;
    }
    const QString sdkDir = qEnvironmentVariable("HIKROBOT_MVS_PYTHON");
    if (!sdkDir.isEmpty()) arguments << QStringLiteral("--sdk-dir") << sdkDir;
    return arguments;
}

void ReferenceForceSource::start()
{
    startWithSession({});
}

void ReferenceForceSource::startSession(const QString &sessionDirectory)
{
    startWithSession(sessionDirectory);
}

void ReferenceForceSource::startWithSession(const QString &sessionDirectory)
{
    if (running()) return;
    m_userStopping = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_lastReceivedWallMs = 0;
    m_ready = false;
    QStringList arguments;
    const QString program = helperInvocation(&arguments);
    if (program.isEmpty()) {
        setStatus(QStringLiteral("标准力相机桥或Python缺失"), false);
        return;
    }
    arguments << QStringLiteral("--hz")
              << qEnvironmentVariable("UCM_REFERENCE_FORCE_HZ",
                                      QStringLiteral("10"))
              << QStringLiteral("--min-confidence")
              << qEnvironmentVariable("UCM_REFERENCE_FORCE_MIN_CONFIDENCE",
                                      QStringLiteral("0.8"))
              << QStringLiteral("--device-index")
              << qEnvironmentVariable("UCM_REFERENCE_FORCE_DEVICE_INDEX",
                                      QStringLiteral("0"));
    arguments += cameraSdkArguments();
    if (!m_roisPath.isEmpty())
        arguments << QStringLiteral("--rois") << m_roisPath;
    if (!sessionDirectory.isEmpty())
        arguments << QStringLiteral("--session-dir") << sessionDirectory;

    m_process.setProgram(program);
    m_process.setArguments(arguments);
    const QString helper = arguments.value(0) == QStringLiteral("-u")
        ? arguments.value(1) : program;
    m_process.setWorkingDirectory(QFileInfo(helper).absolutePath());
    // Make the bundled MVS runtime discoverable without requiring a machine-wide
    // installation. The vendor driver installers remain available separately
    // for GigE/USB3 kernel components that require administrator rights.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString bundledRoot = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("MVS"));
    const QString bundledRuntime = QDir(bundledRoot)
        .filePath(QStringLiteral("Runtime/Win64_x64"));
    const QString bundledPython = QDir(bundledRoot)
        .filePath(QStringLiteral("Development/Samples/Python/MvImport"));
    if (QFileInfo::exists(bundledRuntime)) {
        const QString currentPath = environment.value(QStringLiteral("PATH"));
        environment.insert(QStringLiteral("PATH"), bundledRuntime
            + QDir::listSeparator() + currentPath);
    }
    if (environment.value(QStringLiteral("HIKROBOT_MVS_PYTHON")).isEmpty()
        && QFileInfo::exists(bundledPython)) {
        environment.insert(QStringLiteral("HIKROBOT_MVS_PYTHON"), bundledPython);
    }
    m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    setStatus(QStringLiteral("正在启动标准力相机桥…"), false);
    m_process.start();
}

void ReferenceForceSource::stop()
{
    m_userStopping = true;
    if (running()) {
        m_process.terminate();
        if (!m_process.waitForFinished(1500)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_lastReceivedWallMs = 0;
    setStatus(QStringLiteral("标准力相机已断开"), false);
    m_userStopping = false;
}

void ReferenceForceSource::readStandardOutput()
{
    m_stdoutBuffer += m_process.readAllStandardOutput();
    if (m_stdoutBuffer.size() > 65536) {
        m_stdoutBuffer.clear();
        setStatus(QStringLiteral("标准力相机输出超过限制"), false);
        return;
    }
    int newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line);
    }
}

void ReferenceForceSource::readStandardError()
{
    m_stderrBuffer += m_process.readAllStandardError();
    if (m_stderrBuffer.size() > 65536) {
        m_stderrBuffer.clear();
        setStatus(QStringLiteral("标准力相机状态输出超过限制"), false);
        return;
    }
    int newline = -1;
    while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stderrBuffer.left(newline).trimmed();
        m_stderrBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        const QString message = statusMessageFromStderr(line);
        if (!message.isEmpty()) {
            setStatus(message, m_ready);
        }
    }
}

void ReferenceForceSource::processLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()
        && document.object().value(QStringLiteral("schema")).toString()
            == QStringLiteral("ucm-calibration-camera-frame/v1")) {
        emit cameraSample(document.object());
        return;
    }
    ReferenceForceFrame frame;
    QString error;
    if (!decodeReferenceForcePayload(line, &frame, &error)) {
        setStatus(QStringLiteral("标准力数据被拒绝：%1").arg(error), false);
        return;
    }
    const qint64 wallMs = QDateTime::currentMSecsSinceEpoch();
    if (qAbs(frame.timestampMs - wallMs) > maximumTimestampSkewMs) {
        setStatus(QStringLiteral("标准力数据被拒绝：时间戳与本机不同步"), false);
        return;
    }
    m_latest = frame;
    m_lastReceivedWallMs = wallMs;
    const QString liveStatus = QStringLiteral("标准力在线 · 置信度 %1%")
        .arg(frame.confidence * 100.0, 0, 'f', 1);
    const bool statusWasUnchanged = m_ready && m_statusText == liveStatus;
    setStatus(liveStatus, true);
    if (statusWasUnchanged) emit changed();
}

void ReferenceForceSource::updateStaleness()
{
    if (!m_ready || m_lastReceivedWallMs <= 0) return;
    if (QDateTime::currentMSecsSinceEpoch() - m_lastReceivedWallMs
            > staleAfterMs) {
        setStatus(QStringLiteral("标准力数据已超时，等待相机恢复"), false);
    }
}

void ReferenceForceSource::setStatus(const QString &status, bool ready)
{
    const bool changedState = m_statusText != status || m_ready != ready;
    m_statusText = status;
    m_ready = ready;
    if (changedState) emit changed();
}
