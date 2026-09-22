#pragma once

#include <QByteArray>
#include <QObject>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

struct EthercatPortProbeResult {
    bool success = false;
    bool detected = false;
    QString statusText;
    QString portName;
    QString portDescription;
    QString macAddress;
    QString adapterId;
    int slaveCount = 0;
    int alStatus = 0;
    QVariantList ports;
    QVariantMap evidence;
};

Q_DECLARE_METATYPE(EthercatPortProbeResult)

class EthercatPortScanner final {
public:
    static QByteArray broadcastAlStatusFrame(const QByteArray &sourceMac,
                                             quint8 index);
    static bool parseAlStatusResponse(const QByteArray &frame, quint8 index,
                                      int *workingCounter, int *alStatus);
    static EthercatPortProbeResult scan();
};

class EthercatPortProbeWorker final : public QObject {
    Q_OBJECT

public slots:
    void scan();

signals:
    void completed(const EthercatPortProbeResult &result);
};

class EthercatPortProbe final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool detected READ detected NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString portName READ portName NOTIFY changed)
    Q_PROPERTY(QString portDescription READ portDescription NOTIFY changed)
    Q_PROPERTY(QString macAddress READ macAddress NOTIFY changed)
    Q_PROPERTY(QString adapterId READ adapterId NOTIFY changed)
    Q_PROPERTY(int slaveCount READ slaveCount NOTIFY changed)
    Q_PROPERTY(QString alStateText READ alStateText NOTIFY changed)
    Q_PROPERTY(QVariantList ports READ ports NOTIFY changed)
    Q_PROPERTY(QVariantMap evidence READ evidence NOTIFY changed)

public:
    explicit EthercatPortProbe(bool enabled, QObject *parent = nullptr);
    ~EthercatPortProbe() override;

    bool busy() const { return m_busy; }
    bool detected() const { return m_result.detected; }
    QString statusText() const { return m_result.statusText; }
    QString portName() const { return m_result.portName; }
    QString portDescription() const { return m_result.portDescription; }
    QString macAddress() const { return m_result.macAddress; }
    QString adapterId() const { return m_result.adapterId; }
    int slaveCount() const { return m_result.slaveCount; }
    QString alStateText() const;
    QVariantList ports() const { return m_result.ports; }
    QVariantMap evidence() const { return m_result.evidence; }

    Q_INVOKABLE void scan();

signals:
    void changed();
    void scanRequested();

private:
    bool m_enabled = false;
    bool m_busy = false;
    QThread m_thread;
    EthercatPortProbeWorker *m_worker = nullptr;
    EthercatPortProbeResult m_result;
};
