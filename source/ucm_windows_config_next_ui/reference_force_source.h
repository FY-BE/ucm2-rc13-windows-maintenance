#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include <array>

struct ReferenceForceFrame {
    qint64 timestampMs = 0;
    double confidence = 0.0;
    std::array<double, 4> forceKn {};
};

bool decodeReferenceForcePayload(const QByteArray &line,
                                 ReferenceForceFrame *frame,
                                 QString *error);

class ReferenceForceSource final : public QObject {
    Q_OBJECT

public:
    explicit ReferenceForceSource(QObject *parent = nullptr);
    ~ReferenceForceSource() override;

    bool running() const;
    bool ready() const { return m_ready; }
    QString statusText() const { return m_statusText; }
    ReferenceForceFrame latest() const { return m_latest; }

    static QString helperInvocation(QStringList *prefixArguments);
    static QStringList cameraSdkArguments();
    void setRoisPath(const QString &path) { m_roisPath = path; }
    void start();
    void startSession(const QString &sessionDirectory);
    void stop();

signals:
    void changed();
    void cameraSample(const QJsonObject &sample);

private:
    void readStandardOutput();
    void readStandardError();
    void processLine(const QByteArray &line);
    void updateStaleness();
    void setStatus(const QString &status, bool ready);
    void startWithSession(const QString &sessionDirectory);

    QProcess m_process;
    QTimer m_staleTimer;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    ReferenceForceFrame m_latest;
    qint64 m_lastReceivedWallMs = 0;
    bool m_ready = false;
    bool m_userStopping = false;
    QString m_statusText = QStringLiteral("标准力相机未连接");
    QString m_roisPath;
};
