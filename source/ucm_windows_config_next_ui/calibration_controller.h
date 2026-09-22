#pragma once

#include "config_transport.h"
#include "reference_force_source.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QUrl>

#include <array>

class CalibrationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY changed)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY changed)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY changed)
    Q_PROPERTY(QVariantMap rois READ rois NOTIFY changed)
    Q_PROPERTY(QVariantMap recognition READ recognition NOTIFY changed)
    Q_PROPERTY(bool roiConfirmed READ roiConfirmed NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool stageRecording READ stageRecording NOTIFY changed)
    Q_PROPERTY(int stageCount READ stageCount NOTIFY changed)
    Q_PROPERTY(int cameraCount READ cameraCount NOTIFY changed)
    Q_PROPERTY(int cameraValid READ cameraValid NOTIFY changed)
    Q_PROPERTY(int cameraRejected READ cameraRejected NOTIFY changed)
    Q_PROPERTY(int armCount READ armCount NOTIFY changed)
    Q_PROPERTY(int armValid READ armValid NOTIFY changed)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY changed)
    Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY changed)
    Q_PROPERTY(QVariantList sourceCouplingBiasNs READ sourceCouplingBiasNs NOTIFY changed)
    Q_PROPERTY(QVariantMap fitProgress READ fitProgress NOTIFY changed)
    Q_PROPERTY(QVariantMap liveFit READ liveFit NOTIFY changed)
    Q_PROPERTY(bool candidateAccepted READ candidateAccepted NOTIFY changed)
    Q_PROPERTY(bool manualMode READ manualMode NOTIFY changed)
public:
    explicit CalibrationController(ReferenceForceSource *camera, QObject *parent = nullptr);
    ~CalibrationController() override;
    QString status() const { return m_status; }
    QString previewUrl() const { return m_previewUrl; }
    int imageWidth() const { return m_imageWidth; }
    int imageHeight() const { return m_imageHeight; }
    QVariantMap rois() const { return m_rois.toVariantMap(); }
    QVariantMap recognition() const { return m_recognition.toVariantMap(); }
    bool roiConfirmed() const { return m_roiConfirmed; }
    bool busy() const { return m_tool.state() != QProcess::NotRunning; }
    bool running() const { return m_running; }
    bool stageRecording() const { return m_stageRecording; }
    int stageCount() const { return m_stages.size(); }
    int cameraCount() const { return m_cameraCount; }
    int cameraValid() const { return m_cameraValid; }
    int cameraRejected() const { return m_cameraRejected; }
    int armCount() const { return m_armCount; }
    int armValid() const { return m_armValid; }
    QString sessionPath() const { return m_sessionPath; }
    QVariantMap candidate() const { return m_candidate.toVariantMap(); }
    QVariantList sourceCouplingBiasNs() const {
        return m_model.value(QStringLiteral("coupling_bias_ns")).toArray().toVariantList();
    }
    QVariantMap fitProgress() const { return m_fitProgress.toVariantMap(); }
    QVariantMap liveFit() const { return m_liveFit.toVariantMap(); }
    bool candidateAccepted() const { return m_candidateAccepted; }
    bool manualMode() const { return m_manualMode; }
    bool candidateEvidenceIntact() const;
    QJsonObject sourceModel() const { return m_model; }
    Q_INVOKABLE bool acceptCandidate();
    bool recordArmOperation(int operation, const QVariantMap &result);
    Q_INVOKABLE void capturePreview();
    Q_INVOKABLE void recognizeRois(const QVariantMap &rects);
    Q_INVOKABLE void confirmRois();
    Q_INVOKABLE void cancelRoi();
    Q_INVOKABLE void startCalibration(const QString &operatorName, const QVariantMap &productState);
    Q_INVOKABLE void startManualCalibration(const QString &operatorName, const QVariantMap &productState);
    Q_INVOKABLE void beginStage();
    Q_INVOKABLE void beginManualStage(const QVariantList &forceKn, const QUrl &photo);
    Q_INVOKABLE void endStage();
    Q_INVOKABLE void finishCalibration();
    Q_INVOKABLE void exportSession(const QUrl &destination);
    void armSample(const ucm::TelemetrySnapshot &sample, const QJsonObject &deviceModel);
    void usbDisconnected();
signals:
    void changed();
private:
    enum Tool { None, Capture, Recognize, Fit };
    void runTool(Tool kind, const QStringList &arguments);
    void startPreviewFit();
    void stopPreviewFit();
    void toolFinished(int exitCode);
    void cameraSample(const QJsonObject &sample);
    bool prepareSession(const QString &operatorName, const QVariantMap &productState,
                        bool manualMode);
    bool appendLine(const QString &name, const QJsonObject &row);
    bool writeJson(const QString &path, const QJsonObject &object);
    void fail(const QString &reason);
    QString dataDir() const;
    QString defaultRois() const;
    ReferenceForceSource *m_camera;
    QProcess m_tool;
    QProcess m_previewFitTool;
    Tool m_toolKind = None;
    QString m_status = QStringLiteral("等待 ROI 核对");
    QString m_previewUrl;
    QString m_previewPath;
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    QJsonObject m_rois;
    QJsonObject m_recognition;
    QJsonObject m_identity;
    QJsonObject m_model;
    QJsonArray m_stages;
    QJsonObject m_candidate;
    QJsonObject m_fitProgress;
    QJsonObject m_liveFit;
    QByteArray m_fitStderrBuffer;
    QString m_sessionPath;
    QString m_lastArmIdentity;
    quint64 m_armGeneration = 0;
    quint64 m_armSession = 0;
    bool m_hasArmPublisher = false;
    qint64 m_lastArmUtc = 0;
    qint64 m_lastArmMono = 0;
    qint64 m_lastCameraUtc = 0;
    qint64 m_lastCameraMono = 0;
    QJsonValue m_effectiveThickness;
    bool m_hasThickness = false;
    QTimer m_watchdog;
    QTimer m_previewFitTimer;
    int m_cameraCount = 0;
    int m_cameraValid = 0;
    int m_cameraRejected = 0;
    int m_armCount = 0;
    int m_armValid = 0;
    bool m_roiConfirmed = false;
    bool m_running = false;
    bool m_stageRecording = false;
    bool m_wasCameraRunning = false;
    bool m_candidateAccepted = false;
    bool m_manualMode = false;
    std::array<double, 4> m_manualForceKn {};
    QString m_manualImage;
    QString m_manualImageSha;
    quint64 m_manualSample = 0;
};
