#pragma once

#include "ethercat_master_session.h"

#include <QObject>
#include <QVariantList>

#include <atomic>
#include <thread>

class EthercatMasterController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString profile READ profile NOTIFY changed)
    Q_PROPERTY(QString alState READ alState NOTIFY changed)
    Q_PROPERTY(int alStatusCode READ alStatusCode NOTIFY changed)
    Q_PROPERTY(int workingCounter READ workingCounter NOTIFY changed)
    Q_PROPERTY(int completedCycles READ completedCycles NOTIFY changed)
    Q_PROPERTY(int droppedCycles READ droppedCycles NOTIFY changed)
    Q_PROPERTY(double rod1 READ rod1 NOTIFY changed)
    Q_PROPERTY(double rod2 READ rod2 NOTIFY changed)
    Q_PROPERTY(double rod3 READ rod3 NOTIFY changed)
    Q_PROPERTY(double rod4 READ rod4 NOTIFY changed)
    Q_PROPERTY(double total READ total NOTIFY changed)
    Q_PROPERTY(double windowsTotal READ windowsTotal NOTIFY changed)
    Q_PROPERTY(QString dataTimestampUtcMs READ dataTimestampUtcMs NOTIFY changed)
    Q_PROPERTY(double imbalancePercent READ imbalancePercent NOTIFY changed)
    Q_PROPERTY(int faultFlag READ faultFlag NOTIFY changed)
    Q_PROPERTY(int deviceErrorCode READ deviceErrorCode NOTIFY changed)
    Q_PROPERTY(int configErrorCode READ configErrorCode NOTIFY changed)
    Q_PROPERTY(QVariantMap evidence READ evidence NOTIFY changed)
    Q_PROPERTY(QString evidencePath READ evidencePath NOTIFY changed)
    Q_PROPERTY(QVariantList machineModels READ machineModels CONSTANT)

public:
    explicit EthercatMasterController(bool enabled, QObject *parent = nullptr);
    ~EthercatMasterController() override;

    bool busy() const { return m_busy; }
    bool running() const { return m_running; }
    QString statusText() const { return m_statusText; }
    QString profile() const { return m_profile; }
    QString alState() const { return m_alState; }
    int alStatusCode() const { return m_alStatusCode; }
    int workingCounter() const { return m_workingCounter; }
    int completedCycles() const { return m_completedCycles; }
    int droppedCycles() const { return m_droppedCycles; }
    double rod1() const { return m_input.tieBar1ForceKn; }
    double rod2() const { return m_input.tieBar2ForceKn; }
    double rod3() const { return m_input.tieBar3ForceKn; }
    double rod4() const { return m_input.tieBar4ForceKn; }
    double total() const { return m_input.totalClampingForceKn; }
    double windowsTotal() const {
        return m_input.rodForcesValid()
            ? static_cast<double>(m_input.windowsRodSumKn()) : -1.0;
    }
    QString dataTimestampUtcMs() const { return QString::number(m_input.dataTimestampUtcMs); }
    double imbalancePercent() const { return m_input.loadImbalanceRate / 100.0; }
    int faultFlag() const { return m_input.faultFlag; }
    int deviceErrorCode() const { return m_input.equipmentErrorCode; }
    int configErrorCode() const { return m_input.configurationErrorCode; }
    QVariantMap evidence() const { return m_evidence; }
    QString evidencePath() const { return m_evidencePath; }
    QVariantList machineModels() const;

    Q_INVOKABLE void start(const QString &adapterId, const QString &mac,
                           const QVariantMap &settings);
    Q_INVOKABLE void stop();

signals:
    void changed();

private:
    void joinFinishedThread();
    void applyLive(int alStatus, int alCode, int wkc, int cycles, int inputBytes,
                   const ucm::ethercat::SlaveInput &input);
    void applyResult(const ucm::ethercat::MasterRunResult &result);

    bool m_enabled = false;
    bool m_busy = false;
    bool m_running = false;
    QString m_statusText;
    QString m_profile;
    QString m_alState = QStringLiteral("--");
    int m_alStatusCode = 0;
    int m_workingCounter = 0;
    int m_completedCycles = 0;
    int m_droppedCycles = 0;
    ucm::ethercat::SlaveInput m_input;
    QVariantMap m_evidence;
    QString m_evidencePath;
    std::atomic_bool m_stopRequested {false};
    std::thread m_thread;
};
