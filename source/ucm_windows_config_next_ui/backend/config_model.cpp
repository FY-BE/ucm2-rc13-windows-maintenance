#include "config_model.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace ucm {
namespace {

quint32 crc32(const QByteArray &data)
{
    quint32 value = 0xFFFFFFFFU;
    for (const unsigned char byte : data) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^ ((value & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return value ^ 0xFFFFFFFFU;
}

QString boolJson(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString decimal(double value, int precision)
{
    return QLocale::c().toString(value, 'f', precision);
}

QString switchState(bool value)
{
    return value ? QStringLiteral("启用") : QStringLiteral("禁用");
}

QString integerArray(const std::array<int, 4> &values)
{
    return QStringLiteral("[%1,%2,%3,%4]")
        .arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]);
}

void addIfChanged(QStringList &result, const QString &label, const QString &before, const QString &after)
{
    if (before != after) {
        result << QStringLiteral("%1: %2 → %3").arg(label, before, after);
    }
}

} // namespace

const QVector<AfeGainPoint> &approvedAfeGainPoints()
{
    static const QVector<AfeGainPoint> points {
        {12, 24, 39322}, {12, 24, 34953}, {12, 24, 30583},
        {18, 24, 26214}, {18, 24, 21845}, {18, 30, 17476},
        {24, 30, 13107}, {24, 30, 8738}, {24, 30, 4369},
        {24, 30, 0}
    };
    return points;
}

int approvedAfeGainPointIndex(int lnaGainDb, int pgaGainDb, int vcntlDac)
{
    const QVector<AfeGainPoint> &points = approvedAfeGainPoints();
    for (int index = 0; index < points.size(); ++index) {
        const AfeGainPoint &point = points.at(index);
        if (point.lnaGainDb == lnaGainDb && point.pgaGainDb == pgaGainDb &&
            point.vcntlDac == vcntlDac) {
            return index;
        }
    }
    return -1;
}

int uniformRodLnaGainDb(const AfeConfig &afe)
{
    const int value = afe.lnaGainDbByRod[0];
    for (const int rodValue : afe.lnaGainDbByRod) {
        if (rodValue != value) return -1;
    }
    return value;
}

void setAllRodLnaGainDb(AfeConfig *afe, int lnaGainDb)
{
    if (afe == nullptr) return;
    afe->lnaGainDbByRod.fill(lnaGainDb);
}

QJsonObject Configuration::toJson() const
{
    QJsonObject afeJson {
        {QStringLiteral("center_frequency_khz"), afe.centerFrequencyKhz},
        {QStringLiteral("low_pass_bandwidth_mhz"), afe.lowPassBandwidthMhz},
        {QStringLiteral("pga_gain_db"), afe.pgaGainDb},
        {QStringLiteral("vcntl_dac"), afe.vcntlDac},
        {QStringLiteral("digital_tgc_attenuation_db"), afe.digitalTgcAttenuationDb},
        {QStringLiteral("low_frequency_noise_suppression"), afe.lowFrequencyNoiseSuppression},
        {QStringLiteral("digital_high_pass"), afe.digitalHighPass},
        {QStringLiteral("lna_input_clamp"), afe.lnaInputClamp}
    };
    QJsonArray lnaByRod;
    QJsonArray digitalGainByRod;
    for (int rod = 0; rod < 4; ++rod) {
        lnaByRod.append(afe.lnaGainDbByRod[rod]);
        digitalGainByRod.append(afe.digitalGainStepsByRod[rod]);
    }
    afeJson.insert(QStringLiteral("lna_gain_db_by_rod"), lnaByRod);
    afeJson.insert(QStringLiteral("digital_gain_steps_by_rod"),
                   digitalGainByRod);
    QJsonObject algorithmJson {
        {QStringLiteral("template_confirm_frames"), algorithm.templateConfirmFrames},
        {QStringLiteral("ncc_peak_threshold"), algorithm.nccPeakThreshold},
        {QStringLiteral("rod_length_mm"), algorithm.rodLengthMm},
        {QStringLiteral("measurement_point_mm"), algorithm.measurementPointMm},
        {QStringLiteral("effective_area_mm2"), algorithm.effectiveAreaMm2},
        {QStringLiteral("minimum_valid_force_n"), algorithm.minimumValidForceN},
        {QStringLiteral("imbalance_alarm_threshold"), algorithm.imbalanceAlarmThreshold},
        {QStringLiteral("consecutive_alarm_frames"), algorithm.consecutiveAlarmFrames}
    };
    QJsonArray channels;
    for (const int channel : kFixedAdcChannelOrder) {
        channels.append(channel);
    }
    return QJsonObject {
        {QStringLiteral("schema_id"), schemaId},
        {QStringLiteral("schema_version"), schemaVersion},
        {QStringLiteral("cfg_version"), cfgVersion},
        {QStringLiteral("object_id"), objectId},
        {QStringLiteral("device_profile"), deviceProfile},
        {QStringLiteral("afe"), afeJson},
        {QStringLiteral("algorithm"), algorithmJson},
        {QStringLiteral("adc_channel_map"), channels}
    };
}

QByteArray Configuration::canonicalPayload() const
{
    const QString map = QStringLiteral("[0,1,2,3]");
    const QString payload = QStringLiteral(
        "{\"schema_id\":\"%1\",\"schema_version\":\"%2\",\"cfg_version\":\"%3\","
        "\"object_id\":\"%4\",\"device_profile\":\"%5\",\"afe\":{"
        "\"center_frequency_khz\":%6,\"low_pass_bandwidth_mhz\":%7,"
        "\"lna_gain_db_by_rod\":%8,\"digital_gain_steps_by_rod\":%9,"
        "\"pga_gain_db\":%10,\"vcntl_dac\":%11,"
        "\"digital_tgc_attenuation_db\":%12,\"low_frequency_noise_suppression\":%13,"
        "\"digital_high_pass\":%14,\"lna_input_clamp\":%15},\"algorithm\":{"
        "\"template_confirm_frames\":%16,\"ncc_peak_threshold\":%17,\"rod_length_mm\":%18,"
        "\"measurement_point_mm\":%19,\"effective_area_mm2\":%20,\"minimum_valid_force_n\":%21,"
        "\"imbalance_alarm_threshold\":%22,\"consecutive_alarm_frames\":%23},"
        "\"adc_channel_map\":%24}")
        .arg(schemaId, schemaVersion, cfgVersion, objectId, deviceProfile)
        .arg(afe.centerFrequencyKhz)
        .arg(afe.lowPassBandwidthMhz)
        .arg(integerArray(afe.lnaGainDbByRod))
        .arg(integerArray(afe.digitalGainStepsByRod))
        .arg(afe.pgaGainDb)
        .arg(afe.vcntlDac)
        .arg(afe.digitalTgcAttenuationDb)
        .arg(boolJson(afe.lowFrequencyNoiseSuppression))
        .arg(boolJson(afe.digitalHighPass))
        .arg(boolJson(afe.lnaInputClamp))
        .arg(algorithm.templateConfirmFrames)
        .arg(decimal(algorithm.nccPeakThreshold, 3))
        .arg(algorithm.rodLengthMm)
        .arg(algorithm.measurementPointMm)
        .arg(decimal(algorithm.effectiveAreaMm2, 3))
        .arg(algorithm.minimumValidForceN)
        .arg(decimal(algorithm.imbalanceAlarmThreshold, 3))
        .arg(algorithm.consecutiveAlarmFrames)
        .arg(map);
    return payload.toUtf8();
}

bool Configuration::operator==(const Configuration &other) const
{
    return canonicalPayload() == other.canonicalPayload();
}

QString ValidationResult::summary() const
{
    return isValid() ? QStringLiteral("结构、版本和范围校验通过。")
                     : errors.join(QLatin1Char('\n'));
}

ValidationResult validate(const Configuration &configuration)
{
    ValidationResult result;
    const auto add = [&result](bool invalid, const QString &message) {
        if (invalid) {
            result.errors << message;
        }
    };

    add(configuration.schemaId != QStringLiteral("UCM.CONFIG"), QStringLiteral("schema_id 必须为 UCM.CONFIG。"));
    add(configuration.schemaVersion != QStringLiteral(UCM_CONFIG_SCHEMA_VERSION),
        QStringLiteral("schema_version 与本工具支持版本不一致。"));
    add(!isCanonicalConfigVersion(configuration.cfgVersion),
        QStringLiteral("cfg_version 必须为无前导零的正十进制整数。"));
    add(configuration.objectId.trimmed().isEmpty(), QStringLiteral("配置对象 ID 不能为空。"));
    add(configuration.deviceProfile.trimmed().isEmpty(), QStringLiteral("设备 profile 不能为空。"));
    add(configuration.afe.centerFrequencyKhz != 2500,
        QStringLiteral("当前产品中心频率固定为 2500 kHz。"));
    add(configuration.afe.lowPassBandwidthMhz != 10 && configuration.afe.lowPassBandwidthMhz != 15,
        QStringLiteral("低通带宽只允许 10 或 15 MHz。"));
    for (int rod = 0; rod < 4; ++rod) {
        add(!QVector<int>({12, 18, 24}).contains(
                configuration.afe.lnaGainDbByRod[rod]),
            QStringLiteral("杆%1 LNA 增益只允许 12、18 或 24 dB。")
                .arg(rod + 1));
        add(configuration.afe.digitalGainStepsByRod[rod] < 0
                || configuration.afe.digitalGainStepsByRod[rod] > 30,
            QStringLiteral("杆%1 数字增益步数必须在 0–30。")
                .arg(rod + 1));
    }
    add(!QVector<int>({24, 30}).contains(configuration.afe.pgaGainDb),
        QStringLiteral("PGA 增益只允许 24 或 30 dB。"));
    add(configuration.afe.vcntlDac < 0 || configuration.afe.vcntlDac > 65535,
        QStringLiteral("VCNTL DAC必须在0–65535。"));
    add(!QVector<int>({0, 6, 12, 18, 24, 30, 36, 42})
             .contains(configuration.afe.digitalTgcAttenuationDb),
        QStringLiteral("数字 TGC 衰减只允许 0–42 dB、步进 6 dB。"));
    add(!configuration.afe.lowFrequencyNoiseSuppression,
        QStringLiteral("当前产品必须启用低频噪声抑制。"));
    add(configuration.afe.digitalHighPass,
        QStringLiteral("当前产品必须关闭数字高通。"));
    add(!configuration.afe.lnaInputClamp,
        QStringLiteral("当前产品必须启用 LNA 输入钳位。"));
    add(configuration.algorithm.templateConfirmFrames < 1,
        QStringLiteral("模板确认帧数必须为正整数。"));
    add(!std::isfinite(configuration.algorithm.nccPeakThreshold)
            || configuration.algorithm.nccPeakThreshold < 0.0
            || configuration.algorithm.nccPeakThreshold > 1.0,
        QStringLiteral("NCC 峰值阈值必须在 0–1。"));
    add(configuration.algorithm.rodLengthMm < 1
            || configuration.algorithm.rodLengthMm
                > kMaximumAcousticRodLengthMm,
        QStringLiteral("拉杆总长必须在 1–5839 mm（当前实际采集地址上限）。"));
    add(configuration.algorithm.measurementPointMm < 0
            || configuration.algorithm.measurementPointMm > configuration.algorithm.rodLengthMm,
        QStringLiteral("测点必须位于拉杆总长范围内。"));
    add(!std::isfinite(configuration.algorithm.effectiveAreaMm2)
            || configuration.algorithm.effectiveAreaMm2 <= 0.0,
        QStringLiteral("等效截面积必须是大于 0 的有限数。"));
    add(configuration.algorithm.minimumValidForceN < 0,
        QStringLiteral("最小有效总力不能为负数。"));
    add(!std::isfinite(configuration.algorithm.imbalanceAlarmThreshold)
            || configuration.algorithm.imbalanceAlarmThreshold < 0.0
            || configuration.algorithm.imbalanceAlarmThreshold > 1.0,
        QStringLiteral("偏载率报警阈值必须在 0–1。"));
    add(configuration.algorithm.consecutiveAlarmFrames < 1,
        QStringLiteral("连续报警帧数必须为正整数。"));
    return result;
}

ConfigIdentity identityFor(const Configuration &configuration)
{
    const QByteArray payload = configuration.canonicalPayload();
    const quint32 crc = crc32(payload);
    return {
        QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper(),
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()).toUpper()
    };
}

QStringList diff(const Configuration &baseline, const Configuration &candidate)
{
    QStringList result;
    addIfChanged(result, QStringLiteral("schema"), baseline.schemaVersion, candidate.schemaVersion);
    addIfChanged(result, QStringLiteral("配置版本"), baseline.cfgVersion, candidate.cfgVersion);
    addIfChanged(result, QStringLiteral("对象"), baseline.objectId, candidate.objectId);
    addIfChanged(result, QStringLiteral("设备 profile"), baseline.deviceProfile, candidate.deviceProfile);
    addIfChanged(result, QStringLiteral("AFE 中心频率"), QString::number(baseline.afe.centerFrequencyKhz), QString::number(candidate.afe.centerFrequencyKhz));
    addIfChanged(result, QStringLiteral("低通带宽"), QString::number(baseline.afe.lowPassBandwidthMhz), QString::number(candidate.afe.lowPassBandwidthMhz));
    addIfChanged(result, QStringLiteral("逐杆 LNA 增益"), integerArray(baseline.afe.lnaGainDbByRod), integerArray(candidate.afe.lnaGainDbByRod));
    addIfChanged(result, QStringLiteral("逐杆数字增益"), integerArray(baseline.afe.digitalGainStepsByRod), integerArray(candidate.afe.digitalGainStepsByRod));
    addIfChanged(result, QStringLiteral("PGA 增益"), QString::number(baseline.afe.pgaGainDb), QString::number(candidate.afe.pgaGainDb));
    addIfChanged(result, QStringLiteral("VCNTL DAC"), QString::number(baseline.afe.vcntlDac), QString::number(candidate.afe.vcntlDac));
    addIfChanged(result, QStringLiteral("数字 TGC 衰减"), QString::number(baseline.afe.digitalTgcAttenuationDb), QString::number(candidate.afe.digitalTgcAttenuationDb));
    addIfChanged(result, QStringLiteral("低频噪声抑制"), switchState(baseline.afe.lowFrequencyNoiseSuppression), switchState(candidate.afe.lowFrequencyNoiseSuppression));
    addIfChanged(result, QStringLiteral("数字高通"), switchState(baseline.afe.digitalHighPass), switchState(candidate.afe.digitalHighPass));
    addIfChanged(result, QStringLiteral("LNA 输入钳位"), switchState(baseline.afe.lnaInputClamp), switchState(candidate.afe.lnaInputClamp));
    addIfChanged(result, QStringLiteral("模板确认帧数"), QString::number(baseline.algorithm.templateConfirmFrames), QString::number(candidate.algorithm.templateConfirmFrames));
    addIfChanged(result, QStringLiteral("NCC 阈值"), decimal(baseline.algorithm.nccPeakThreshold, 3), decimal(candidate.algorithm.nccPeakThreshold, 3));
    addIfChanged(result, QStringLiteral("拉杆总长"), QString::number(baseline.algorithm.rodLengthMm), QString::number(candidate.algorithm.rodLengthMm));
    addIfChanged(result, QStringLiteral("测点"), QString::number(baseline.algorithm.measurementPointMm), QString::number(candidate.algorithm.measurementPointMm));
    addIfChanged(result, QStringLiteral("等效截面积"), decimal(baseline.algorithm.effectiveAreaMm2, 3), decimal(candidate.algorithm.effectiveAreaMm2, 3));
    addIfChanged(result, QStringLiteral("最小有效总力"), QString::number(baseline.algorithm.minimumValidForceN), QString::number(candidate.algorithm.minimumValidForceN));
    addIfChanged(result, QStringLiteral("偏载率阈值"), decimal(baseline.algorithm.imbalanceAlarmThreshold, 3), decimal(candidate.algorithm.imbalanceAlarmThreshold, 3));
    addIfChanged(result, QStringLiteral("连续报警帧数"), QString::number(baseline.algorithm.consecutiveAlarmFrames), QString::number(candidate.algorithm.consecutiveAlarmFrames));
    return result;
}

bool isCanonicalConfigVersion(const QString &value)
{
    if (value.isEmpty() || value == QStringLiteral("0")
        || (value.size() > 1 && value.startsWith(QLatin1Char('0')))) {
        return false;
    }
    for (const QChar character : value) {
        if (!character.isDigit()) {
            return false;
        }
    }
    bool ok = false;
    value.toULongLong(&ok, 10);
    return ok;
}

QString nextConfigVersion(const QString &value)
{
    if (!isCanonicalConfigVersion(value)) {
        return {};
    }
    bool ok = false;
    const qulonglong current = value.toULongLong(&ok, 10);
    if (!ok || current == std::numeric_limits<qulonglong>::max()) {
        return {};
    }
    return QString::number(current + 1u);
}

} // namespace ucm
