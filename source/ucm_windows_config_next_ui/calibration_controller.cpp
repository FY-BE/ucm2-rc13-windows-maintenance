#include "calibration_controller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#include <cmath>

namespace {
QJsonObject loadObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool validForceCorrection(const QJsonObject &candidate)
{
    if (candidate.value(QStringLiteral("schema")).toString()
        != QStringLiteral("ucm-windows-calibration-candidate/v3"))
        return false;
    const QJsonObject correction =
        candidate.value(QStringLiteral("force_correction")).toObject();
    const QString model = correction.value(QStringLiteral("model")).toString();
    const int count = correction.value(QStringLiteral("knot_count")).toInt(-1);
    const QJsonArray inputs = correction.value(QStringLiteral("input_force_n")).toArray();
    const QJsonArray outputs = correction.value(QStringLiteral("output_force_n")).toArray();
    if (model == QStringLiteral("IDENTITY"))
        return count == 0 && inputs.isEmpty() && outputs.isEmpty();
    if (model != QStringLiteral("MONOTONE_PWL_ZERO_V1") ||
        count < 2 || count > 8 || inputs.size() != count || outputs.size() != count)
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
    return true;
}
}

CalibrationController::CalibrationController(ReferenceForceSource *camera, QObject *parent)
    : QObject(parent), m_camera(camera)
{
    const QString saved = QDir(dataDir()).filePath(QStringLiteral("confirmed-rois.json"));
    m_rois = loadObject(QFileInfo::exists(saved) ? saved : defaultRois());
    m_roiConfirmed = false;
    if (QFileInfo::exists(saved)) m_camera->setRoisPath(saved);
    connect(&m_tool, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { toolFinished(code); });
    connect(&m_tool, &QProcess::readyReadStandardError, this, [this] {
        if (m_toolKind != Fit) return;
        m_fitStderrBuffer += m_tool.readAllStandardError();
        while (true) {
            const qsizetype newline = m_fitStderrBuffer.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = m_fitStderrBuffer.left(newline).trimmed();
            m_fitStderrBuffer.remove(0, newline + 1);
            const QJsonObject event = QJsonDocument::fromJson(line).object();
            if (event.value(QStringLiteral("kind")).toString() != QStringLiteral("fit_progress")) continue;
            m_fitProgress = event;
            m_status = QStringLiteral("正在拟合：") + event.value(QStringLiteral("message")).toString();
            emit changed();
        }
    });
    connect(&m_previewFitTool, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        if (!m_running || code != 0 || status != QProcess::NormalExit) return;
        const QJsonObject result = QJsonDocument::fromJson(m_previewFitTool.readAllStandardOutput()).object();
        if (result.value(QStringLiteral("preview")).toBool()
            && result.value(QStringLiteral("session_id")).toString() == QFileInfo(m_sessionPath).fileName()) {
            m_liveFit = result;
            emit changed();
        }
    });
    m_previewFitTimer.setInterval(2000);
    connect(&m_previewFitTimer, &QTimer::timeout, this, &CalibrationController::startPreviewFit);
    connect(m_camera, &ReferenceForceSource::cameraSample,
            this, &CalibrationController::cameraSample);
    connect(m_camera, &ReferenceForceSource::changed, this, [this] {
        if (m_running && !m_camera->running()
            && (m_camera->statusText().contains(QStringLiteral("已退出"))
                || m_camera->statusText().contains(QStringLiteral("启动失败"))))
            fail(QStringLiteral("相机连接中断"));
    });
    m_watchdog.setInterval(500);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (!m_running) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_stageRecording && (now - m_lastCameraUtc > 1000
            || now - m_lastArmUtc > 1000)) {
            fail(QStringLiteral("档位期间相机或 USB 数据超时")); return;
        }
        QStorageInfo storage(m_sessionPath);
        if (!storage.isValid() || storage.bytesAvailable() < 256LL * 1024LL * 1024LL)
            fail(QStringLiteral("磁盘空间不足"));
    });
}

CalibrationController::~CalibrationController()
{
    if (m_running) fail(QStringLiteral("程序关闭"));
}

QString CalibrationController::dataDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/calibration");
}

QString CalibrationController::defaultRois() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("reference_force_camera/vendor/windows/force_input_camera/rois.json"));
}

bool CalibrationController::writeJson(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}

bool CalibrationController::appendLine(const QString &name, const QJsonObject &row)
{
    QFile file(QDir(m_sessionPath).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    const QByteArray line = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
    return file.write(line) == line.size() && file.flush();
}

void CalibrationController::runTool(Tool kind, const QStringList &extra)
{
    if (busy()) return;
    QStringList args;
    const QString program = ReferenceForceSource::helperInvocation(&args);
    if (program.isEmpty()) { m_status = QStringLiteral("相机桥不可用"); emit changed(); return; }
    args += extra;
    args += ReferenceForceSource::cameraSdkArguments();
    m_toolKind = kind;
    if (kind == Fit) {
        m_fitStderrBuffer.clear();
        m_fitProgress = {{QStringLiteral("step"), QStringLiteral("starting")},
                         {QStringLiteral("percent"), 0},
                         {QStringLiteral("message"), QStringLiteral("正在启动拟合")}};
    }
    m_tool.setProgram(program);
    m_tool.setArguments(args);
    m_tool.setWorkingDirectory(QFileInfo(args.value(0) == QStringLiteral("-u") ? args.value(1) : program).absolutePath());
    m_tool.start();
    emit changed();
}

void CalibrationController::startPreviewFit()
{
    if (!m_running || m_stages.isEmpty() || m_previewFitTool.state() != QProcess::NotRunning) return;
    QJsonArray stages = m_stages;
    if (m_stageRecording) {
        QJsonObject current = stages.last().toObject();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - qint64(current.value(QStringLiteral("start_utc_ms")).toDouble()) < 2000) return;
        current.insert(QStringLiteral("end_utc_ms"), now);
        stages[stages.size() - 1] = current;
    }
    if (!writeJson(QDir(m_sessionPath).filePath(QStringLiteral("preview-stages.json")),
                   {{QStringLiteral("stages"), stages}})) return;
    QStringList args;
    const QString program = ReferenceForceSource::helperInvocation(&args);
    if (program.isEmpty()) return;
    args += {QStringLiteral("--fit-session"), m_sessionPath, QStringLiteral("--preview-fit")};
    m_previewFitTool.setProgram(program);
    m_previewFitTool.setArguments(args);
    m_previewFitTool.setWorkingDirectory(QFileInfo(args.value(0) == QStringLiteral("-u")
        ? args.value(1) : program).absolutePath());
    m_previewFitTool.start();
}

void CalibrationController::stopPreviewFit()
{
    m_previewFitTimer.stop();
    if (m_previewFitTool.state() != QProcess::NotRunning) {
        m_previewFitTool.kill();
        m_previewFitTool.waitForFinished(1000);
    }
    if (!m_sessionPath.isEmpty())
        QFile::remove(QDir(m_sessionPath).filePath(QStringLiteral("preview-stages.json")));
}

void CalibrationController::capturePreview()
{
    if (m_running || busy()) return;
    m_wasCameraRunning = m_camera->running();
    m_camera->stop();
    QDir().mkpath(dataDir());
    m_previewPath = QDir(dataDir()).filePath(QStringLiteral("roi-preview.bmp"));
    QFile::remove(m_previewPath);
    QFile::remove(m_previewPath + QStringLiteral(".writing"));
    m_previewUrl.clear();
    m_imageWidth = 0;
    m_imageHeight = 0;
    m_recognition = {};
    m_status = QStringLiteral("正在拍摄 ROI 预览");
    runTool(Capture, {QStringLiteral("--capture-image"), m_previewPath});
}

void CalibrationController::recognizeRois(const QVariantMap &rects)
{
    if (m_previewPath.isEmpty() || busy() || m_running) return;
    const QJsonObject proposed = QJsonObject::fromVariantMap(rects);
    for (const char *field : {"S1", "S2", "S3", "S4", "Total", "Other"}) {
        const QJsonArray box = proposed.value(QLatin1String(field)).toArray();
        if (box.size() != 4 || box[0].toInt(-1) < 0 || box[1].toInt(-1) < 0
            || box[2].toInt() <= 0 || box[3].toInt() <= 0) {
            m_status = QStringLiteral("六个 ROI 框均需有效"); emit changed(); return;
        }
    }
    const QString pending = QDir(dataDir()).filePath(QStringLiteral("pending-rois.json"));
    if (!writeJson(pending, proposed)) { m_status = QStringLiteral("ROI 工作副本写入失败"); emit changed(); return; }
    m_rois = proposed;
    m_recognition = {};
    m_status = QStringLiteral("正在识别 S1–S4");
    runTool(Recognize, {QStringLiteral("--recognize-image"), m_previewPath,
                        QStringLiteral("--rois"), pending});
}

void CalibrationController::confirmRois()
{
    if (m_running || busy() || m_recognition.value(QStringLiteral("status")) != QStringLiteral("ok")
        || m_recognition.value(QStringLiteral("confidence")).toDouble() < 0.9) return;
    const QJsonObject values = m_recognition.value(QStringLiteral("values")).toObject();
    for (const char *name : {"S1", "S2", "S3", "S4"}) {
        const QJsonValue value = values.value(QLatin1String(name));
        bool valid = value.isDouble();
        const double force = value.isDouble() ? value.toDouble() : value.toString().toDouble(&valid);
        if (!valid || !std::isfinite(force) || force < 0) return;
    }
    const QString path = QDir(dataDir()).filePath(QStringLiteral("confirmed-rois.json"));
    QJsonObject confirmed = m_rois;
    confirmed.insert(QStringLiteral("_image_size"), QJsonArray{m_imageWidth, m_imageHeight});
    if (!writeJson(path, confirmed)) { m_status = QStringLiteral("确认 ROI 写入失败"); emit changed(); return; }
    m_rois = confirmed;
    m_camera->setRoisPath(path);
    m_roiConfirmed = true;
    m_status = QStringLiteral("ROI 已确认，可关闭窗口并开始标定");
    if (m_wasCameraRunning) m_camera->start();
    emit changed();
}

void CalibrationController::cancelRoi()
{
    if (busy()) {
        m_toolKind = None;
        m_tool.kill();
        m_tool.waitForFinished(1000);
    }
    m_rois = loadObject(QDir(dataDir()).filePath(QStringLiteral("confirmed-rois.json")));
    if (m_rois.isEmpty()) m_rois = loadObject(defaultRois());
    m_recognition = {};
    m_status = QStringLiteral("已取消 ROI 修改");
    if (m_wasCameraRunning && !m_camera->running()) m_camera->start();
    emit changed();
}

void CalibrationController::toolFinished(int exitCode)
{
    const Tool kind = m_toolKind;
    m_toolKind = None;
    if (kind == None) return;
    const QByteArray outputBytes = m_tool.readAllStandardOutput().trimmed();
    QJsonParseError parseError;
    const QJsonObject response = QJsonDocument::fromJson(outputBytes, &parseError).object();
    if (exitCode != 0 || response.isEmpty()) {
        if (kind == Fit) {
            m_candidate = loadObject(QDir(m_sessionPath).filePath(QStringLiteral("candidate.json")));
            m_status = m_candidate.value(QStringLiteral("reason")).toString(QStringLiteral("拟合未完成；证据已保存"));
            m_fitProgress = {{QStringLiteral("step"), QStringLiteral("finished")},
                             {QStringLiteral("percent"), 100},
                             {QStringLiteral("message"), m_status}};
        } else {
            const QByteArray errorBytes = m_tool.readAllStandardError().trimmed();
            const QJsonObject errorObject = QJsonDocument::fromJson(errorBytes).object();
            QString detail = errorObject.value(QStringLiteral("message")).toString();
            if (detail.isEmpty() && !errorBytes.isEmpty())
                detail = QString::fromUtf8(errorBytes).left(240);
            if (detail.isEmpty() && !outputBytes.isEmpty())
                detail = QStringLiteral("相机桥输出无法解析（%1）").arg(parseError.errorString());
            if (detail.isEmpty())
                detail = QStringLiteral("相机桥无输出，退出码 %1，进程状态：%2")
                             .arg(exitCode).arg(m_tool.errorString());
            m_status = QStringLiteral("相机操作失败：%1").arg(detail);
        }
        emit changed(); return;
    }
    if (kind == Capture) {
        m_imageWidth = response.value(QStringLiteral("width")).toInt();
        m_imageHeight = response.value(QStringLiteral("height")).toInt();
        // The source is cleared before every capture, so the same local URL
        // reloads without a query string that some image loaders treat as a
        // part of the BMP filename.
        m_previewUrl = QUrl::fromLocalFile(m_previewPath).toString(QUrl::FullyEncoded);
        m_status = QStringLiteral("预览已拍摄；调整或沿用六个 ROI 后识别");
    } else if (kind == Recognize) {
        m_recognition = response;
        m_status = response.value(QStringLiteral("status")) == QStringLiteral("ok")
            && response.value(QStringLiteral("confidence")).toDouble() >= 0.9
            ? QStringLiteral("请核对 S1–S4，正确后确认 ROI")
            : QStringLiteral("识别未过门限，请重画或重拍");
    } else if (kind == Fit) {
        m_candidate = loadObject(QDir(m_sessionPath).filePath(QStringLiteral("candidate.json")));
        m_candidateAccepted = false;
        m_status = QStringLiteral("拟合完成：本地候选，未经计量批准");
        m_fitProgress = {{QStringLiteral("step"), QStringLiteral("finished")},
                         {QStringLiteral("percent"), 100},
                         {QStringLiteral("message"), m_status}};
    }
    emit changed();
}

void CalibrationController::startCalibration(const QString &operatorName, const QVariantMap &productState)
{
    if (m_running || busy() || !m_roiConfirmed || operatorName.trimmed().isEmpty()) return;
    if (!prepareSession(operatorName, productState, false)) return;
    m_camera->stop();
    m_camera->setRoisPath(QDir(m_sessionPath).filePath(QStringLiteral("confirmed-rois.json")));
    m_camera->startSession(m_sessionPath);
    m_status = QStringLiteral("相机标定会话已开始；请逐档划定稳定区间");
    emit changed();
}

void CalibrationController::startManualCalibration(
    const QString &operatorName, const QVariantMap &productState)
{
    if (m_running || busy() || operatorName.trimmed().isEmpty()) return;
    if (!prepareSession(operatorName, productState, true)) return;
    m_camera->stop();
    m_status = QStringLiteral("人工标定会话已开始；输入本档四杆标准力并选择现场照片");
    emit changed();
}

bool CalibrationController::prepareSession(const QString &operatorName,
                                            const QVariantMap &productState,
                                            bool manualMode)
{
    const QJsonObject state = QJsonObject::fromVariantMap(productState);
    const QJsonObject device = state.value(QStringLiteral("deviceModel")).toObject();
    m_model = device.value(QStringLiteral("active_document")).toObject();
    m_identity = device.value(QStringLiteral("active_identity")).toObject();
    if (!state.value(QStringLiteral("success")).toBool() || m_model.isEmpty() || m_identity.isEmpty()) {
        m_status = QStringLiteral("ARM 活动型号配置或身份缺失"); emit changed(); return false;
    }
    QDir().mkpath(dataDir());
    const QString unique = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"))
        + QStringLiteral("-%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    m_sessionPath = QDir(dataDir()).filePath(QStringLiteral("sessions/") + unique);
    if (QFileInfo::exists(m_sessionPath)
        || !QDir().mkpath(m_sessionPath + QStringLiteral("/camera"))) {
        m_status = QStringLiteral("会话目录创建失败"); emit changed(); return false;
    }
    QStorageInfo storage(m_sessionPath);
    if (!storage.isValid() || storage.bytesAvailable() < 1024LL * 1024LL * 1024LL) {
        m_status = QStringLiteral("磁盘可用空间不足 1 GiB"); emit changed(); return false;
    }
    QByteArray roiBytes;
    QString provenanceSha;
    if (!manualMode) {
        const QString roiPath = QDir(dataDir()).filePath(QStringLiteral("confirmed-rois.json"));
        QFile roi(roiPath);
        if (!roi.open(QIODevice::ReadOnly)) {
            m_status = QStringLiteral("确认 ROI 文件缺失"); emit changed(); return false;
        }
        roiBytes = roi.readAll();
        QFile copy(QDir(m_sessionPath).filePath(QStringLiteral("confirmed-rois.json")));
        if (!copy.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || copy.write(roiBytes) != roiBytes.size()) {
            m_status = QStringLiteral("ROI 证据写入失败"); emit changed(); return false;
        }
        copy.close();
        QFile provenance(QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("reference_force_camera/vendor/windows/force_input_camera/SOURCE_PROVENANCE.json")));
        provenanceSha = provenance.open(QIODevice::ReadOnly) ? digest(provenance.readAll()) : QString();
    }
    const QJsonObject context{{QStringLiteral("schema"), QStringLiteral("ucm-calibration-context/v1")},
        {QStringLiteral("operator"), operatorName.trimmed()},
        {QStringLiteral("started_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("active_model"), m_model}, {QStringLiteral("active_model_identity"), m_identity},
        {QStringLiteral("reference_source"), manualMode ? QStringLiteral("manual") : QStringLiteral("camera")},
        {QStringLiteral("roi_sha256"), manualMode ? QString() : digest(roiBytes)},
        {QStringLiteral("vendor_provenance_sha256"), provenanceSha}};
    if (!writeJson(QDir(m_sessionPath).filePath(QStringLiteral("context.json")), context)
        || !writeJson(QDir(m_sessionPath).filePath(QStringLiteral("session-state.json")),
                      {{QStringLiteral("status"), QStringLiteral("recording")}})) {
        m_status = QStringLiteral("会话上下文写入失败"); emit changed(); return false;
    }
    m_stages = {};
    m_candidate = {};
    m_fitProgress = {};
    m_liveFit = {};
    m_candidateAccepted = false;
    m_cameraCount = m_cameraValid = m_cameraRejected = m_armCount = m_armValid = 0;
    m_lastArmIdentity.clear();
    m_hasArmPublisher = false;
    m_lastArmUtc = m_lastArmMono = m_lastCameraUtc = m_lastCameraMono = 0;
    m_effectiveThickness = QJsonValue();
    m_hasThickness = false;
    m_manualMode = manualMode;
    m_manualImage.clear();
    m_manualImageSha.clear();
    m_manualSample = 0;
    m_running = true;
    if (!manualMode) m_roiConfirmed = false;
    m_stageRecording = false;
    m_watchdog.start();
    m_previewFitTimer.start();
    return true;
}

void CalibrationController::beginStage()
{
    if (m_manualMode) {
        m_status = QStringLiteral("人工模式请填写四杆标准力并选择现场照片"); emit changed(); return;
    }
    if (!m_running || m_stageRecording || !m_camera->ready()
        || QDateTime::currentMSecsSinceEpoch() - m_lastArmUtc > 1000) {
        m_status = QStringLiteral("等待相机和 USB 新鲜数据后再开始本档"); emit changed(); return;
    }
    QSaveFile flag(QDir(m_sessionPath).filePath(QStringLiteral("recording.flag")));
    if (!flag.open(QIODevice::WriteOnly) || flag.write("recording\n") != 10 || !flag.commit()) {
        fail(QStringLiteral("档位记录标志写入失败")); return;
    }
    m_stages.append(QJsonObject{{QStringLiteral("start_utc_ms"), QDateTime::currentMSecsSinceEpoch()}});
    m_stageRecording = true;
    m_status = QStringLiteral("正在记录第 %1 档").arg(m_stages.size());
    emit changed();
}

void CalibrationController::beginManualStage(const QVariantList &forceKn,
                                             const QUrl &photo)
{
    if (!m_running || !m_manualMode || m_stageRecording || forceKn.size() != 4
        || !photo.isLocalFile() || QDateTime::currentMSecsSinceEpoch() - m_lastArmUtc > 1000) {
        m_status = QStringLiteral("人工本档需要新鲜USB、四杆标准力和一张本机现场照片"); emit changed(); return;
    }
    for (int rod = 0; rod < 4; ++rod) {
        bool ok = false;
        const double value = forceKn.at(rod).toDouble(&ok);
        if (!ok || !std::isfinite(value) || value < 0.0) {
            m_status = QStringLiteral("人工标准力必须是四个非负有限数值（kN）"); emit changed(); return;
        }
        m_manualForceKn[rod] = value;
    }
    QFile source(photo.toLocalFile());
    if (!source.open(QIODevice::ReadOnly)) {
        m_status = QStringLiteral("现场照片无法读取"); emit changed(); return;
    }
    const QByteArray bytes = source.readAll();
    const QString suffix = QFileInfo(source).suffix().isEmpty() ? QStringLiteral("bin")
                                                                : QFileInfo(source).suffix().toLower();
    m_manualImage = QStringLiteral("camera/manual-stage-%1.%2").arg(m_stages.size() + 1).arg(suffix);
    QFile destination(QDir(m_sessionPath).filePath(m_manualImage));
    if (!destination.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || destination.write(bytes) != bytes.size()) {
        m_status = QStringLiteral("现场照片证据写入失败"); emit changed(); return;
    }
    destination.close();
    m_manualImageSha = digest(bytes);
    QSaveFile flag(QDir(m_sessionPath).filePath(QStringLiteral("recording.flag")));
    if (!flag.open(QIODevice::WriteOnly) || flag.write("recording\n") != 10 || !flag.commit()) {
        fail(QStringLiteral("档位记录标志写入失败")); return;
    }
    m_stages.append(QJsonObject{{QStringLiteral("start_utc_ms"), QDateTime::currentMSecsSinceEpoch()},
                                {QStringLiteral("reference_source"), QStringLiteral("manual")},
                                {QStringLiteral("photo"), m_manualImage}});
    m_stageRecording = true;
    m_status = QStringLiteral("正在记录第 %1 档（人工标准力）").arg(m_stages.size());
    emit changed();
}

void CalibrationController::endStage()
{
    if (!m_stageRecording) return;
    const QString flag = QDir(m_sessionPath).filePath(QStringLiteral("recording.flag"));
    if (!QFile::remove(flag) && QFileInfo::exists(flag)) {
        fail(QStringLiteral("档位记录标志无法关闭")); return;
    }
    QJsonObject stage = m_stages.last().toObject();
    stage.insert(QStringLiteral("end_utc_ms"), QDateTime::currentMSecsSinceEpoch());
    m_stages[m_stages.size() - 1] = stage;
    m_stageRecording = false;
    if (!writeJson(QDir(m_sessionPath).filePath(QStringLiteral("stages.json")),
                   {{QStringLiteral("stages"), m_stages}})) {
        fail(QStringLiteral("档位证据写入失败")); return;
    }
    m_status = QStringLiteral("第 %1 档已结束").arg(m_stages.size());
    startPreviewFit();
    emit changed();
}

void CalibrationController::finishCalibration()
{
    if (!m_running) return;
    if (m_stageRecording) endStage();
    if (!m_running) return;
    m_running = false;
    m_watchdog.stop();
    stopPreviewFit();
    QFile::remove(QDir(m_sessionPath).filePath(QStringLiteral("recording.flag")));
    m_camera->stop();
    if (!m_manualMode)
        m_camera->setRoisPath(QDir(dataDir()).filePath(QStringLiteral("confirmed-rois.json")));
    QJsonObject state{{QStringLiteral("status"), QStringLiteral("complete")},
                      {QStringLiteral("finished_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (!writeJson(QDir(m_sessionPath).filePath(QStringLiteral("session-state.json")), state)) {
        m_status = QStringLiteral("会话状态写入失败"); emit changed(); return;
    }
    m_status = QStringLiteral("正在拟合本地候选");
    runTool(Fit, {QStringLiteral("--fit-session"), m_sessionPath});
    emit changed();
}

bool CalibrationController::acceptCandidate()
{
    if (m_running || busy() || m_candidate.value(QStringLiteral("status")).toString() != QStringLiteral("candidate")
        || m_sessionPath.isEmpty()) return false;
    if (m_candidate.value(QStringLiteral("fit_constraint")).toString()
        != QStringLiteral("shared_kmat_shared_b") ||
        !m_candidate.value(QStringLiteral("b_shared_ns")).isDouble() ||
        !std::isfinite(m_candidate.value(QStringLiteral("b_shared_ns")).toDouble()) ||
        !m_candidate.value(QStringLiteral("kmat_unified")).isDouble() ||
        !std::isfinite(m_candidate.value(QStringLiteral("kmat_unified")).toDouble()) ||
        m_candidate.value(QStringLiteral("kmat_unified")).toDouble() <= 0 ||
        !validForceCorrection(m_candidate))
        return false;
    const QJsonArray offsets = m_candidate.value(QStringLiteral("coupling_bias_ns")).toArray();
    if (offsets.size() != 4) return false;
    for (const QJsonValue &offset : offsets)
        if (!offset.isDouble() || !std::isfinite(offset.toDouble())) return false;
    if (offsets != m_model.value(QStringLiteral("coupling_bias_ns")).toArray() ||
        m_candidate.contains(QStringLiteral("b_total_ns"))) return false;
    const QString path = QDir(m_sessionPath).filePath(QStringLiteral("candidate.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    if (QJsonDocument::fromJson(bytes).object() != m_candidate) return false;
    const QJsonObject review{{QStringLiteral("schema"), QStringLiteral("ucm-calibration-review/v1")},
        {QStringLiteral("session_id"), m_candidate.value(QStringLiteral("session_id"))},
        {QStringLiteral("candidate_sha256"), digest(bytes)},
        {QStringLiteral("accepted_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("scope"), QStringLiteral("candidate-only; not metrological approval")}};
    const QString reviewPath = QDir(m_sessionPath).filePath(QStringLiteral("candidate-review.json"));
    if (QFileInfo::exists(reviewPath) || !writeJson(reviewPath, review)) {
        m_status = QStringLiteral("候选确认记录写入失败；未确认"); emit changed(); return false;
    }
    m_candidateAccepted = true;
    m_status = QStringLiteral("候选已人工确认，尚未下发 ARM");
    emit changed();
    return true;
}

bool CalibrationController::candidateEvidenceIntact() const
{
    if (!m_candidateAccepted || m_sessionPath.isEmpty()) return false;
    QFile candidateFile(QDir(m_sessionPath).filePath(QStringLiteral("candidate.json")));
    if (!candidateFile.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = candidateFile.readAll();
    const QJsonObject review = loadObject(QDir(m_sessionPath).filePath(QStringLiteral("candidate-review.json")));
    return QJsonDocument::fromJson(bytes).object() == m_candidate
        && review.value(QStringLiteral("candidate_sha256")).toString() == digest(bytes)
        && review.value(QStringLiteral("session_id")) == m_candidate.value(QStringLiteral("session_id"));
}

bool CalibrationController::recordArmOperation(int operation, const QVariantMap &result)
{
    if (!m_candidateAccepted || m_sessionPath.isEmpty()) return false;
    const QString path = QDir(m_sessionPath).filePath(QStringLiteral("arm-operations.json"));
    const QJsonObject previous = loadObject(path);
    if (QFileInfo::exists(path) && previous.value(QStringLiteral("schema")).toString()
            != QStringLiteral("ucm-calibration-arm-operations/v1")) return false;
    QJsonArray events = previous.value(QStringLiteral("events")).toArray();
    events.append(QJsonObject{{QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("result"), QJsonObject::fromVariantMap(result)}});
    return writeJson(path, {{QStringLiteral("schema"), QStringLiteral("ucm-calibration-arm-operations/v1")},
                            {QStringLiteral("events"), events}});
}

void CalibrationController::fail(const QString &reason)
{
    if (!m_running) return;
    m_running = false;
    m_watchdog.stop();
    stopPreviewFit();
    QFile::remove(QDir(m_sessionPath).filePath(QStringLiteral("recording.flag")));
    m_stageRecording = false;
    m_camera->stop();
    if (!m_manualMode)
        m_camera->setRoisPath(QDir(dataDir()).filePath(QStringLiteral("confirmed-rois.json")));
    writeJson(QDir(m_sessionPath).filePath(QStringLiteral("stages.json")),
              {{QStringLiteral("stages"), m_stages}});
    writeJson(QDir(m_sessionPath).filePath(QStringLiteral("session-state.json")),
              {{QStringLiteral("status"), QStringLiteral("incomplete")},
               {QStringLiteral("reason"), reason},
               {QStringLiteral("finished_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    m_status = QStringLiteral("会话不完整：") + reason;
    emit changed();
}

void CalibrationController::usbDisconnected() { fail(QStringLiteral("USB 连接中断")); }

void CalibrationController::exportSession(const QUrl &destination)
{
    if (m_running || busy() || m_sessionPath.isEmpty() || !destination.isLocalFile()) return;
    const QDir source(m_sessionPath);
    const QString target = QDir(destination.toLocalFile()).filePath(source.dirName());
    if (QFileInfo::exists(target) || !QDir().mkpath(target)) {
        m_status = QStringLiteral("导出目录已存在或不可写"); emit changed(); return;
    }
    QDirIterator files(m_sessionPath, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString file = files.next();
        const QString relative = source.relativeFilePath(file);
        const QString output = QDir(target).filePath(relative);
        if (!QDir().mkpath(QFileInfo(output).absolutePath()) || !QFile::copy(file, output)) {
            m_status = QStringLiteral("导出中断，已复制文件保留在：") + target;
            emit changed(); return;
        }
    }
    m_status = QStringLiteral("已导出至：") + target;
    emit changed();
}

void CalibrationController::cameraSample(const QJsonObject &sample)
{
    if (!m_running) return;
    const qint64 utc = sample.value(QStringLiteral("timestamp_ms")).toVariant().toLongLong();
    const qint64 mono = sample.value(QStringLiteral("monotonic_ns")).toVariant().toLongLong();
    if (m_lastCameraUtc && std::abs((utc - m_lastCameraUtc) - (mono - m_lastCameraMono) / 1000000.0) > 100)
        { fail(QStringLiteral("相机侧 Windows 时钟跳变")); return; }
    m_lastCameraUtc = utc; m_lastCameraMono = mono;
    if (!appendLine(QStringLiteral("camera.jsonl"), sample)) { fail(QStringLiteral("相机证据写入失败")); return; }
    ++m_cameraCount;
    const QString quality = sample.value(QStringLiteral("status")).toString();
    if (quality == QStringLiteral("ok")) ++m_cameraValid;
    else if (quality == QStringLiteral("rejected")) ++m_cameraRejected;
    if (m_cameraCount % 5 == 0) emit changed();
}

void CalibrationController::armSample(const ucm::TelemetrySnapshot &sample, const QJsonObject &deviceModel)
{
    if (!m_running || !sample.productResult) return;
    if (deviceModel.value(QStringLiteral("active_identity")).toObject() != m_identity)
        { fail(QStringLiteral("ARM 配置身份变化")); return; }
    if (m_hasArmPublisher && (sample.generation != m_armGeneration
        || sample.sessionId != m_armSession)) {
        fail(QStringLiteral("ARM 发布者或采集会话变化")); return;
    }
    m_armGeneration = sample.generation;
    m_armSession = sample.sessionId;
    m_hasArmPublisher = true;
    const QJsonValue thickness = sample.pairedInputStatus.value(QStringLiteral("effectiveThicknessUm"));
    if (sample.pairedInputStatus.value(QStringLiteral("moldValid")).toInt() != 1
        || !thickness.isDouble() || thickness.toDouble() <= 0) {
        fail(QStringLiteral("同帧生效模厚缺失")); return;
    }
    if (m_hasThickness && thickness != m_effectiveThickness) {
        fail(QStringLiteral("会话中几何量变化")); return;
    }
    m_effectiveThickness = thickness;
    m_hasThickness = true;
    const QString identity = QStringLiteral("%1/%2/%3/%4/%5/%6")
        .arg(sample.generation).arg(sample.publishedMonotonicNs).arg(sample.sessionId)
        .arg(sample.sequence).arg(sample.frameCounter).arg(sample.captureRequestId);
    if (identity == m_lastArmIdentity) return;
    if (m_lastArmUtc && std::abs((sample.observedUtcMs - m_lastArmUtc)
        - (sample.observedMonotonicNs - m_lastArmMono) / 1000000.0) > 100)
        { fail(QStringLiteral("USB 侧 Windows 时钟跳变")); return; }
    m_lastArmIdentity = identity;
    m_lastArmUtc = sample.observedUtcMs; m_lastArmMono = sample.observedMonotonicNs;
    QJsonArray delays;
    for (const auto &rod : sample.rod) delays.append(rod.delayNs);
    const QJsonObject row{{QStringLiteral("observed_utc_ms"), sample.observedUtcMs},
        {QStringLiteral("observed_monotonic_ns"), sample.observedMonotonicNs},
        {QStringLiteral("published_monotonic_ns"), QString::number(sample.publishedMonotonicNs)},
        {QStringLiteral("generation"), QString::number(sample.generation)},
        {QStringLiteral("session_id"), QString::number(sample.sessionId)},
        {QStringLiteral("sequence"), QString::number(sample.sequence)},
        {QStringLiteral("frame_counter"), QString::number(sample.frameCounter)},
        {QStringLiteral("capture_request_id"), QString::number(sample.captureRequestId)},
        {QStringLiteral("measurement_mask"), int(sample.measurementValidMask)},
        {QStringLiteral("ncc_delta_ns"), delays},
        {QStringLiteral("active_model_identity"), m_identity},
        {QStringLiteral("effective_thickness_um"), sample.pairedInputStatus.value(QStringLiteral("effectiveThicknessUm"))},
        {QStringLiteral("paired_input_status"), sample.pairedInputStatus}};
    if (!appendLine(QStringLiteral("arm.jsonl"), row)) { fail(QStringLiteral("USB 证据写入失败")); return; }
    ++m_armCount;
    if ((sample.measurementValidMask & 15U) == 15U) ++m_armValid;
    if (m_manualMode && m_stageRecording) {
        QJsonArray force;
        for (double value : m_manualForceKn) force.append(value);
        const QJsonObject reference {
            {QStringLiteral("sample_id"), QStringLiteral("manual-%1-%2")
                 .arg(m_stages.size()).arg(++m_manualSample)},
            {QStringLiteral("timestamp_ms"), sample.observedUtcMs},
            {QStringLiteral("monotonic_ns"), sample.observedMonotonicNs},
            {QStringLiteral("status"), QStringLiteral("ok")},
            {QStringLiteral("confidence"), 1.0},
            {QStringLiteral("force_kN"), force},
            {QStringLiteral("image"), m_manualImage},
            {QStringLiteral("image_sha256"), m_manualImageSha},
            {QStringLiteral("reference_source"), QStringLiteral("manual")}
        };
        if (!appendLine(QStringLiteral("camera.jsonl"), reference)) {
            fail(QStringLiteral("人工标准力证据写入失败")); return;
        }
        m_lastCameraUtc = sample.observedUtcMs;
        m_lastCameraMono = sample.observedMonotonicNs;
        ++m_cameraCount;
        ++m_cameraValid;
    }
    if (m_armCount % 10 == 0) emit changed();
}
