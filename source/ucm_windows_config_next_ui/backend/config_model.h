#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

#ifndef UCM_CONFIG_SCHEMA_VERSION
#define UCM_CONFIG_SCHEMA_VERSION "2.0"
#endif

namespace ucm {

// Legacy wire bytes only; ARM adc_map_valid requires this fixed order.
inline constexpr std::array<int, 4> kFixedAdcChannelOrder {{0, 1, 2, 3}};

constexpr int kMaximumAcousticRodLengthMm = 5839;

struct AfeConfig {
    int centerFrequencyKhz = 2500;
    int lowPassBandwidthMhz = 10;
    std::array<int, 4> lnaGainDbByRod {{18, 18, 18, 18}};
    std::array<int, 4> digitalGainStepsByRod {{0, 0, 0, 0}};
    int pgaGainDb = 24;
    int vcntlDac = 21845;
    int digitalTgcAttenuationDb = 0;
    bool lowFrequencyNoiseSuppression = true;
    bool digitalHighPass = false;
    bool lnaInputClamp = true;
};

struct AfeGainPoint {
    int lnaGainDb = 0;
    int pgaGainDb = 0;
    int vcntlDac = 0;
};

const QVector<AfeGainPoint> &approvedAfeGainPoints();
int approvedAfeGainPointIndex(int lnaGainDb, int pgaGainDb, int vcntlDac);
int uniformRodLnaGainDb(const AfeConfig &afe);
void setAllRodLnaGainDb(AfeConfig *afe, int lnaGainDb);

struct AlgorithmConfig {
    int templateConfirmFrames = 20;
    double nccPeakThreshold = 0.900;
    int rodLengthMm = 2500;
    int measurementPointMm = 300;
    double effectiveAreaMm2 = 50.0;
    int minimumValidForceN = 1000;
    double imbalanceAlarmThreshold = 0.20;
    int consecutiveAlarmFrames = 5;
};

struct Configuration {
    QString schemaId = QStringLiteral("UCM.CONFIG");
    QString schemaVersion = QStringLiteral(UCM_CONFIG_SCHEMA_VERSION);
    QString cfgVersion = QStringLiteral("1");
    QString objectId = QStringLiteral("UCB1");
    QString deviceProfile = QStringLiteral("UCM-R02-DEMO");
    AfeConfig afe;
    AlgorithmConfig algorithm;

    QJsonObject toJson() const;
    QByteArray canonicalPayload() const;
    bool operator==(const Configuration &other) const;
    bool operator!=(const Configuration &other) const { return !(*this == other); }
};

struct ConfigIdentity {
    QString crc32;
    QString sha256;
};

struct ValidationResult {
    QStringList errors;
    bool isValid() const { return errors.isEmpty(); }
    QString summary() const;
};

ValidationResult validate(const Configuration &configuration);
ConfigIdentity identityFor(const Configuration &configuration);
QStringList diff(const Configuration &baseline, const Configuration &candidate);
bool isCanonicalConfigVersion(const QString &value);
QString nextConfigVersion(const QString &value);

} // namespace ucm
