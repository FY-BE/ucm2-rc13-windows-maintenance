#include "usb_wire_v1.h"

#include <QtEndian>

#include <cmath>
#include <cstring>
#include <limits>

namespace ucm {
namespace {

constexpr quint16 kResponseFlag = 0x0001U;
constexpr quint16 kErrorFlag = 0x0002U;
constexpr quint16 kAllFlags = 0x000FU;
constexpr quint32 kCapabilitiesToken = 0x31504355U;
constexpr quint32 kConfigToken = 0x31474643U;
constexpr quint32 kReceiptToken = 0x31524355U;
constexpr quint32 kTelemetryToken = 0x31544D55U;
constexpr quint32 kLogListToken = 0x314C4755U;
constexpr quint32 kTelemetryFlagsAll = 0x000001FFU;

void appendU16(QByteArray &bytes, quint16 value)
{
    const quint16 le = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&le), sizeof(le));
}

void appendU32(QByteArray &bytes, quint32 value)
{
    const quint32 le = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&le), sizeof(le));
}

void appendU64(QByteArray &bytes, quint64 value)
{
    const quint64 le = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&le), sizeof(le));
}

quint16 readU16(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

qint32 readI32(const QByteArray &bytes, int offset)
{
    return static_cast<qint32>(readU32(bytes, offset));
}

quint64 readU64(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

double readF64(const QByteArray &bytes, int offset)
{
    const quint64 bits = readU64(bytes, offset);
    double value = 0.0;
    static_assert(sizeof(value) == sizeof(bits), "binary64 size mismatch");
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool allZero(const QByteArray &bytes)
{
    for (const char value : bytes) {
        if (value != 0) {
            return false;
        }
    }
    return true;
}

quint32 scaled(double value, double factor)
{
    return static_cast<quint32>(std::llround(value * factor));
}

QByteArray hexDigest(const QString &hex)
{
    if (hex.size() != 64) {
        return {};
    }
    const QByteArray value = QByteArray::fromHex(hex.toLatin1());
    return value.size() == 32 ? value : QByteArray {};
}

Configuration decodeConfiguration(const QByteArray &bytes, int offset,
                                  QString *error)
{
    Configuration configuration;
    if (bytes.size() < offset + 256
        || readU32(bytes, offset) != kConfigToken
        || readU16(bytes, offset + 4) != kUsbWireAbiV1
        || readU16(bytes, offset + 6) != 256
        || readU32(bytes, offset + 8) != 0
        || readU32(bytes, offset + 12) != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 配置对象头无效。");
        }
        return {};
    }
    const QByteArray object = bytes.mid(offset, 256);
    QByteArray crcImage = object;
    memset(crcImage.data() + 252, 0, 4);
    if (readU32(object, 252) != usbCrc32V1(crcImage)
        || !allZero(object.mid(160, 92))) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 配置对象 CRC 或保留位无效。");
        }
        return {};
    }
    configuration.cfgVersion = QString::number(readU64(object, 32));
    configuration.deviceProfile = QStringLiteral("UCM-DEVICE-%1")
        .arg(readU32(object, 124), 8, 16, QLatin1Char('0')).toUpper();
    configuration.afe.centerFrequencyKhz = static_cast<int>(readU32(object, 48));
    configuration.afe.lowPassBandwidthMhz = static_cast<int>(readU32(object, 52));
    setAllRodLnaGainDb(&configuration.afe,
        static_cast<int>(readU32(object, 56)));
    configuration.afe.pgaGainDb = static_cast<int>(readU32(object, 60));
    configuration.afe.vcntlDac = static_cast<int>(readU32(object, 64));
    configuration.afe.digitalTgcAttenuationDb = static_cast<int>(readU32(object, 68));
    configuration.afe.lowFrequencyNoiseSuppression = readU32(object, 72) == 1U;
    configuration.afe.digitalHighPass = readU32(object, 76) == 1U;
    configuration.afe.lnaInputClamp = readU32(object, 80) == 1U;
    configuration.algorithm.templateConfirmFrames = static_cast<int>(readU32(object, 84));
    configuration.algorithm.nccPeakThreshold = readU32(object, 88) / 1000000.0;
    configuration.algorithm.rodLengthMm = static_cast<int>(readU32(object, 92));
    configuration.algorithm.measurementPointMm = static_cast<int>(readU32(object, 96));
    configuration.algorithm.effectiveAreaMm2 = readU32(object, 100) / 1000.0;
    configuration.algorithm.minimumValidForceN = static_cast<int>(readU32(object, 104));
    configuration.algorithm.imbalanceAlarmThreshold = readU32(object, 108) / 1000000.0;
    configuration.algorithm.consecutiveAlarmFrames = static_cast<int>(readU32(object, 112));
    for (int index = 0; index < 4; ++index) {
        if (static_cast<unsigned char>(object.at(116 + index)) != kFixedAdcChannelOrder[index]) {
            if (error) *error = QStringLiteral("设备回读硬件顺序不符合固定四杆协议。");
            return {};
        }
    }
    if (!validate(configuration).isValid() && error != nullptr) {
        *error = QStringLiteral("设备回读的配置值未通过本地白名单。");
    }
    return configuration;
}

} // namespace

quint32 usbCrc32V1(const QByteArray &bytes)
{
    quint32 value = 0xFFFFFFFFU;
    for (const unsigned char byte : bytes) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1U)
                ^ ((value & 1U) != 0U ? 0xEDB88320U : 0U);
        }
    }
    return value ^ 0xFFFFFFFFU;
}

QString usbWireStatusTextV1(qint32 status)
{
    switch (status) {
    case 0: return QStringLiteral("OK · 成功");
    case 1: return QStringLiteral("INVALID_ARGUMENT · 参数无效");
    case 2: return QStringLiteral("UNSUPPORTED · 不支持");
    case 3: return QStringLiteral("LENGTH_ERROR · 长度错误");
    case 4: return QStringLiteral("HEADER_CRC_ERROR · 帧头CRC错误");
    case 5: return QStringLiteral("PAYLOAD_CRC_ERROR · 载荷CRC错误");
    case 6: return QStringLiteral("CONFLICT · 状态冲突");
    case 7: return QStringLiteral("NOT_FOUND · ARM快照尚未建立");
    case 8: return QStringLiteral("IO_ERROR · ARM文件I/O错误");
    case 9: return QStringLiteral("BUSY · ARM资源忙或快照未稳定");
    case 10: return QStringLiteral("DENIED · 控制权不足");
    case 11: return QStringLiteral("PENDING · 等待终态");
    case 12: return QStringLiteral("STALE · 对象已过期");
    default: return QStringLiteral("UNKNOWN · 未知状态");
    }
}

QByteArray encodeUsbFrameV1(UsbMessageTypeV1 type, quint16 flags,
                            quint64 sequence, quint64 transactionId,
                            const QByteArray &payload, QString *error)
{
    if (sequence == 0 || (flags & ~kAllFlags) != 0
        || payload.size() > static_cast<int>(kUsbWirePayloadMaximumV1)) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 帧参数越界。");
        }
        return {};
    }
    QByteArray header;
    appendU32(header, kUsbWireMagicV1);
    appendU16(header, kUsbWireAbiV1);
    appendU16(header, kUsbWireHeaderBytesV1);
    appendU16(header, static_cast<quint16>(type));
    appendU16(header, flags);
    appendU32(header, static_cast<quint32>(payload.size()));
    appendU64(header, sequence);
    appendU64(header, transactionId);
    appendU32(header, payload.isEmpty() ? 0U : usbCrc32V1(payload));
    appendU32(header, 0U);
    const quint32 headerCrc = usbCrc32V1(header);
    const quint32 headerCrcLe = qToLittleEndian(headerCrc);
    memcpy(header.data() + 36, &headerCrcLe, sizeof(headerCrcLe));
    return header + payload;
}

bool decodeUsbFrameHeaderV1(const QByteArray &header,
                            UsbFrameV1 *frame, quint32 *payloadBytes,
                            quint32 *payloadCrc32, QString *error)
{
    if (header.size() != kUsbWireHeaderBytesV1 || frame == nullptr
        || payloadBytes == nullptr || payloadCrc32 == nullptr) {
        if (error != nullptr) *error = QStringLiteral("USB 响应头长度无效。");
        return false;
    }
    QByteArray crcImage = header;
    memset(crcImage.data() + 36, 0, 4);
    const quint16 flags = readU16(header, 10);
    const quint32 length = readU32(header, 12);
    if (readU32(header, 0) != kUsbWireMagicV1
        || readU16(header, 4) != kUsbWireAbiV1
        || readU16(header, 6) != kUsbWireHeaderBytesV1
        || (flags & ~kAllFlags) != 0 || (flags & kResponseFlag) == 0
        || length > kUsbWirePayloadMaximumV1
        || readU64(header, 16) == 0
        || readU32(header, 36) != usbCrc32V1(crcImage)) {
        if (error != nullptr) *error = QStringLiteral("USB 响应头合同或 CRC 无效。");
        return false;
    }
    frame->messageType = readU16(header, 8);
    frame->flags = flags;
    frame->sequence = readU64(header, 16);
    frame->transactionId = readU64(header, 24);
    *payloadBytes = length;
    *payloadCrc32 = readU32(header, 32);
    return true;
}

bool finishUsbFrameV1(UsbFrameV1 *frame, const QByteArray &payload,
                      quint32 expectedPayloadBytes,
                      quint32 expectedPayloadCrc32, QString *error)
{
    if (frame == nullptr
        || payload.size() != static_cast<int>(expectedPayloadBytes)
        || (payload.isEmpty() ? 0U : usbCrc32V1(payload))
            != expectedPayloadCrc32) {
        if (error != nullptr) *error = QStringLiteral("USB 响应载荷长度或 CRC 无效。");
        return false;
    }
    frame->payload = payload;
    return true;
}

quint64 usbConfigurationChangedMaskV1(const Configuration &a,
                                      const Configuration &b)
{
    quint64 mask = 0;
    const auto changed = [&mask](bool value, int bit) {
        if (value) mask |= 1ULL << bit;
    };
    changed(a.afe.centerFrequencyKhz != b.afe.centerFrequencyKhz, 0);
    changed(a.afe.lowPassBandwidthMhz != b.afe.lowPassBandwidthMhz, 1);
    changed(a.afe.lnaGainDbByRod != b.afe.lnaGainDbByRod, 2);
    changed(a.afe.pgaGainDb != b.afe.pgaGainDb, 3);
    changed(a.afe.vcntlDac != b.afe.vcntlDac, 4);
    changed(a.afe.digitalTgcAttenuationDb != b.afe.digitalTgcAttenuationDb, 5);
    changed(a.afe.lowFrequencyNoiseSuppression != b.afe.lowFrequencyNoiseSuppression, 6);
    changed(a.afe.digitalHighPass != b.afe.digitalHighPass, 7);
    changed(a.afe.lnaInputClamp != b.afe.lnaInputClamp, 8);
    changed(a.algorithm.templateConfirmFrames != b.algorithm.templateConfirmFrames, 9);
    changed(a.algorithm.nccPeakThreshold != b.algorithm.nccPeakThreshold, 10);
    changed(a.algorithm.rodLengthMm != b.algorithm.rodLengthMm, 11);
    changed(a.algorithm.measurementPointMm != b.algorithm.measurementPointMm, 12);
    changed(a.algorithm.effectiveAreaMm2 != b.algorithm.effectiveAreaMm2, 13);
    changed(a.algorithm.minimumValidForceN != b.algorithm.minimumValidForceN, 14);
    changed(a.algorithm.imbalanceAlarmThreshold != b.algorithm.imbalanceAlarmThreshold, 15);
    changed(a.algorithm.consecutiveAlarmFrames != b.algorithm.consecutiveAlarmFrames, 16);
    // Legacy bit 17 has no editable field; keep its wire position reserved.
    return mask;
}

QByteArray encodeUsbConfigurationV1(const Configuration &configuration,
                                    quint64 transactionId,
                                    quint64 baseGeneration,
                                    quint64 changedFieldMask,
                                    QString *error)
{
    const int lnaGainDb = uniformRodLnaGainDb(configuration.afe);
    bool generationOk = false;
    const quint64 candidateGeneration = configuration.cfgVersion.toULongLong(&generationOk);
    const ConfigIdentity identity = identityFor(configuration);
    const QByteArray sha = hexDigest(identity.sha256);
    if (!validate(configuration).isValid() || !generationOk
        || transactionId == 0 || baseGeneration == 0
        || baseGeneration == std::numeric_limits<quint64>::max()
        || candidateGeneration != baseGeneration + 1U
        || changedFieldMask == 0 || (changedFieldMask & (1ULL << 17)) != 0 || lnaGainDb < 0
        || (changedFieldMask & ~kUsbConfigFieldsAllV1) != 0
        || sha.size() != 32) {
        if (error != nullptr) *error = QStringLiteral("完整配置对象不满足 USB 白名单合同。");
        return {};
    }
    QByteArray bytes;
    appendU32(bytes, kConfigToken);
    appendU16(bytes, kUsbWireAbiV1);
    appendU16(bytes, 256U);
    appendU32(bytes, 0U);
    appendU32(bytes, 0U);
    appendU64(bytes, transactionId);
    appendU64(bytes, baseGeneration);
    appendU64(bytes, candidateGeneration);
    appendU64(bytes, changedFieldMask);
    appendU32(bytes, static_cast<quint32>(configuration.afe.centerFrequencyKhz));
    appendU32(bytes, static_cast<quint32>(configuration.afe.lowPassBandwidthMhz));
    appendU32(bytes, static_cast<quint32>(lnaGainDb));
    appendU32(bytes, static_cast<quint32>(configuration.afe.pgaGainDb));
    appendU32(bytes, static_cast<quint32>(configuration.afe.vcntlDac));
    appendU32(bytes, static_cast<quint32>(configuration.afe.digitalTgcAttenuationDb));
    appendU32(bytes, configuration.afe.lowFrequencyNoiseSuppression ? 1U : 0U);
    appendU32(bytes, configuration.afe.digitalHighPass ? 1U : 0U);
    appendU32(bytes, configuration.afe.lnaInputClamp ? 1U : 0U);
    appendU32(bytes, static_cast<quint32>(configuration.algorithm.templateConfirmFrames));
    appendU32(bytes, scaled(configuration.algorithm.nccPeakThreshold, 1000000.0));
    appendU32(bytes, static_cast<quint32>(configuration.algorithm.rodLengthMm));
    appendU32(bytes, static_cast<quint32>(configuration.algorithm.measurementPointMm));
    appendU32(bytes, scaled(configuration.algorithm.effectiveAreaMm2, 1000.0));
    appendU32(bytes, static_cast<quint32>(configuration.algorithm.minimumValidForceN));
    appendU32(bytes, scaled(configuration.algorithm.imbalanceAlarmThreshold, 1000000.0));
    appendU32(bytes, static_cast<quint32>(configuration.algorithm.consecutiveAlarmFrames));
    for (const int channel : kFixedAdcChannelOrder) bytes.append(static_cast<char>(channel));
    appendU32(bytes, 1U);
    appendU32(bytes, identity.crc32.toUInt(nullptr, 16));
    bytes.append(sha);
    bytes.append(QByteArray(92, '\0'));
    appendU32(bytes, 0U);
    if (bytes.size() != 256) {
        if (error != nullptr) *error = QStringLiteral("USB 配置对象内部长度错误。");
        return {};
    }
    const quint32 crcLe = qToLittleEndian(usbCrc32V1(bytes));
    memcpy(bytes.data() + 252, &crcLe, sizeof(crcLe));
    return bytes;
}

bool decodeUsbCapabilitiesV1(const QByteArray &payload,
                             UsbCapabilitiesV1 *capabilities,
                             QString *error)
{
    if (capabilities == nullptr || payload.size() != 128
        || readU32(payload, 0) != kCapabilitiesToken
        || readU16(payload, 4) != kUsbWireAbiV1
        || readU16(payload, 6) != 128 || readU32(payload, 12) != kUsbWirePayloadMaximumV1
        || readU32(payload, 44) != 0 || readU32(payload, 48) != 1) {
        if (error != nullptr) *error = QStringLiteral("USB capability 回执无效。");
        return false;
    }
    capabilities->configFieldMask = readU64(payload, 16);
    capabilities->logSourceMask = readU64(payload, 32);
    capabilities->waveformFlags = readU32(payload, 40);
    const quint32 vidPid = readU32(payload, 52);
    capabilities->vendorId = static_cast<quint16>(vidPid >> 16);
    capabilities->productId = static_cast<quint16>(vidPid & 0xFFFFU);
    capabilities->outEndpoint = static_cast<quint8>(readU32(payload, 56));
    capabilities->inEndpoint = static_cast<quint8>(readU32(payload, 60));
    capabilities->buildId = QString::fromLatin1(payload.mid(96, 32).constData());
    // The V2-only product keeps ABI1 discovery/telemetry framing, but the
    // legacy V1 configuration surface must be disabled.  Requiring the old
    // all-fields mask here rejects the correct V2-only ARM before extended
    // capability discovery can even begin.
    if (capabilities->configFieldMask != 0U
        || capabilities->logSourceMask != 0x0FULL
        || capabilities->waveformFlags != 1U
        || capabilities->vendorId != 0x1D6B || capabilities->productId != 0x0105
        || capabilities->outEndpoint != 0x01 || capabilities->inEndpoint != 0x81
        || capabilities->buildId.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "设备 capability 与 V2-only bootstrap 合同不一致。");
        }
        return false;
    }
    return true;
}

bool decodeUsbProductCapabilitiesV9(const QByteArray &payload,
                                    UsbCapabilitiesV1 *capabilities,
                                    QString *error)
{
    if (capabilities) *capabilities = {};
    const QByteArray identity("UCM.CONFIG.USB.FUNCTIONFS.V1/1D6B:0105/OUT01/IN81");
    auto fixedString = [&](int offset, int bytes) {
        const auto field = payload.mid(offset, bytes);
        const auto end = field.indexOf('\0');
        return end > 0 && allZero(field.mid(end));
    };
    if (!capabilities || payload.size() != 128
        || readU32(payload, 0) != kCapabilitiesToken
        || readU16(payload, 4) != 1 || readU16(payload, 6) != 128
        || readU32(payload, 8) != 1 || readU32(payload, 12) != 262144
        || !allZero(payload.mid(16, 24)) || readU32(payload, 40) != 1
        || readU32(payload, 44) != 0 || readU32(payload, 48) != 1
        || readU32(payload, 52) != 0x1d6b0105U
        || readU32(payload, 56) != 1 || readU32(payload, 60) != 0x81
        || readU32(payload, 64) != usbCrc32V1(identity)
        || !fixedString(68, 28) || !fixedString(96, 32)) {
        if (error) *error = QStringLiteral("协议不兼容：产品USB基础能力、身份或旧接口禁用掩码不匹配。");
        return false;
    }
    capabilities->vendorId = 0x1d6b;
    capabilities->productId = 0x0105;
    capabilities->outEndpoint = 1;
    capabilities->inEndpoint = 0x81;
    capabilities->waveformFlags = 1;
    capabilities->buildId = QString::fromLatin1(payload.mid(96, 32).split('\0').first());
    return true;
}

bool decodeUsbConfigReceiptV1(const QByteArray &payload,
                              UsbConfigReceiptV1 *receipt,
                              QString *error)
{
    if (receipt == nullptr || payload.size() != 512
        || readU32(payload, 0) != kReceiptToken
        || readU16(payload, 4) != kUsbWireAbiV1
        || readU16(payload, 6) != 512) {
        if (error != nullptr) *error = QStringLiteral("USB 配置回执头无效。");
        return false;
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + 8, 0, 4);
    if (readU32(payload, 8) != usbCrc32V1(crcImage)
        || readU32(payload, 20) != 0 || readU32(payload, 24) != 1
        || readU32(payload, 28) != 0) {
        if (error != nullptr) *error = QStringLiteral("USB 配置回执 CRC 或易失合同无效。");
        return false;
    }
    QString configError;
    Configuration active = decodeConfiguration(payload, 72, &configError);
    if (!configError.isEmpty()) {
        if (error != nullptr) *error = configError;
        return false;
    }
    receipt->kind = readU32(payload, 12);
    receipt->result = readI32(payload, 16);
    receipt->transactionId = readU64(payload, 32);
    receipt->baseGeneration = readU64(payload, 40);
    receipt->requestedGeneration = readU64(payload, 48);
    receipt->activeGeneration = readU64(payload, 56);
    receipt->changedFieldMask = readU64(payload, 64);
    receipt->activeConfiguration = active;
    receipt->candidateSha256 = payload.mid(72 + 128, 32);
    for (int index = 0; index < 18; ++index) {
        receipt->fieldResults[index] = readI32(payload, 328 + index * 4);
    }
    receipt->hardwareReceipt = payload.mid(400, 112);
    QByteArray hardwareCrc = receipt->hardwareReceipt;
    memset(hardwareCrc.data() + 108, 0, 4);
    receipt->hardwareReceiptValid = readU32(receipt->hardwareReceipt, 0) == 0x31464355U
        && readU32(receipt->hardwareReceipt, 4) == 1U
        && readU32(receipt->hardwareReceipt, 8) == 112U
        && readU32(receipt->hardwareReceipt, 24) == 0U
        && readU64(receipt->hardwareReceipt, 64) != 0U
        && readU64(receipt->hardwareReceipt, 72) != 0U
        && readU32(receipt->hardwareReceipt, 84) != 0U
        && readU32(receipt->hardwareReceipt, 88) != 0U
        && readU32(receipt->hardwareReceipt, 92) != 0U
        && readU32(receipt->hardwareReceipt, 96) != 0U
        && readU32(receipt->hardwareReceipt, 108) == usbCrc32V1(hardwareCrc);
    receipt->hardware.valid = receipt->hardwareReceiptValid;
    receipt->hardware.stateKind = readU32(receipt->hardwareReceipt, 12);
    receipt->hardware.result = readI32(receipt->hardwareReceipt, 16);
    receipt->hardware.digitalTgcAttenuationDb =
        readU32(receipt->hardwareReceipt, 20);
    receipt->hardware.persisted =
        readU32(receipt->hardwareReceipt, 24) != 0U;
    receipt->hardware.transactionId =
        readU64(receipt->hardwareReceipt, 32);
    receipt->hardware.baseGeneration =
        readU64(receipt->hardwareReceipt, 40);
    receipt->hardware.requestedGeneration =
        readU64(receipt->hardwareReceipt, 48);
    receipt->hardware.activeGeneration =
        readU64(receipt->hardwareReceipt, 56);
    receipt->hardware.frontendSessionId =
        readU64(receipt->hardwareReceipt, 64);
    receipt->hardware.frontendSessionGeneration =
        readU64(receipt->hardwareReceipt, 72);
    receipt->hardware.requestCrc32 =
        readU32(receipt->hardwareReceipt, 80);
    receipt->hardware.afeProfileCrc32 =
        readU32(receipt->hardwareReceipt, 84);
    receipt->hardware.afeReceiptSequence =
        readU32(receipt->hardwareReceipt, 88);
    receipt->hardware.trainingReceiptSequence =
        readU32(receipt->hardwareReceipt, 92);
    receipt->hardware.cadenceReceiptSequence =
        readU32(receipt->hardwareReceipt, 96);
    if (receipt->kind < 1U || receipt->kind > 3U
        || receipt->activeGeneration == 0U
        || receipt->activeConfiguration.cfgVersion
            != QString::number(receipt->activeGeneration)
        || !receipt->hardwareReceiptValid) {
        if (error != nullptr) *error = QStringLiteral("USB 配置回执字段或硬件回执无效。");
        return false;
    }
    return true;
}

bool usbConfigurationValuesEqualV1(const Configuration &left,
                                   const Configuration &right)
{
    return usbConfigurationChangedMaskV1(left, right) == 0;
}

bool decodeUsbTelemetrySnapshotV1(const QByteArray &payload,
                                  TelemetrySnapshot *snapshot,
                                  QString *error)
{
    if (snapshot == nullptr || payload.size() != 512
        || readU32(payload, 0) != kTelemetryToken
        || readU16(payload, 4) != kUsbWireAbiV1
        || readU16(payload, 6) != 512) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 遥测对象头无效。");
        }
        return false;
    }
    QByteArray crcImage = payload;
    memset(crcImage.data() + 8, 0, 4);
    const quint32 flags = readU32(payload, 12);
    if (readU32(payload, 8) != usbCrc32V1(crcImage)
        || (flags & ~kTelemetryFlagsAll) != 0U
        || readU64(payload, 16) == 0U
        || readU64(payload, 24) == 0U
        || readU64(payload, 32) == 0U
        || readU64(payload, 40) == 0U
        || readU64(payload, 48) == 0U
        || readU32(payload, 64) == 0U
        || readU32(payload, 64) > 8192U
        || (readU32(payload, 68) & ~0x0FU) != 0U
        || (readU32(payload, 72) & ~0x0FU) != 0U
        || (readU32(payload, 76) & ~0x0FU) != 0U
        || (readU32(payload, 80) & ~0x0FU) != 0U
        || readU32(payload, 132) > 42U
        || (readU32(payload, 132) % 6U) != 0U
        || !allZero(payload.mid(488, 24))) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 遥测对象 CRC、范围或保留位无效。");
        }
        return false;
    }

    TelemetrySnapshot decoded;
    decoded.success = true;
    decoded.message = QStringLiteral("最新 ARM 完整遥测读取成功。");
    decoded.flags = flags;
    decoded.generation = readU64(payload, 16);
    decoded.publishedMonotonicNs = readU64(payload, 24);
    decoded.sessionId = readU64(payload, 32);
    decoded.sequence = readU64(payload, 40);
    decoded.frameCounter = readU64(payload, 48);
    decoded.profile = readU32(payload, 56);
    decoded.windowStart = readU32(payload, 60);
    decoded.windowLength = readU32(payload, 64);
    decoded.measurementValidMask = readU32(payload, 68);
    decoded.forceAvailableMask = readU32(payload, 72);
    decoded.rodValidMask = readU32(payload, 76);
    decoded.negativeForceMask = readU32(payload, 80);
    decoded.processState = readU32(payload, 84);
    decoded.processStateTrusted = readU32(payload, 88) != 0U;
    decoded.producerStatusFlags = readU16(payload, 92);
    decoded.producerStatusCoverage = readU16(payload, 94);
    decoded.combinedStatusFlags = readU16(payload, 96);
    decoded.combinedStatusCoverage = readU16(payload, 98);
    decoded.healthState = readU32(payload, 100);
    decoded.primaryReasonCode = readU32(payload, 104);
    decoded.afeProfileCrc32 = readU32(payload, 108);
    decoded.afeReceiptSequence = readU32(payload, 112);
    decoded.trainingReceiptSequence = readU32(payload, 116);
    decoded.cadenceReceiptSequence = readU32(payload, 120);
    decoded.hvSetpoint = readU32(payload, 124);
    decoded.hvReceiptSequence = readU32(payload, 128);
    decoded.digitalTgcAttenuationDb = readU32(payload, 132);
    decoded.formalTotalN = readF64(payload, 144);
    decoded.imbalanceIndex = readF64(payload, 152);
    decoded.k0NsPerN = readF64(payload, 160);
    decoded.formalForceValid = (flags & 0x00000008U) != 0U;

    for (int rod = 0; rod < 4; ++rod) {
        const int offset = 168 + rod * 80;
        RodTelemetrySnapshot &target = decoded.rod[rod];
        target.measurementFlags = readU32(payload, offset);
        target.saturationCount = readU32(payload, offset + 4);
        target.forceReason = readU32(payload, offset + 8);
        if (readU32(payload, offset + 12) != 0U) {
            if (error != nullptr) {
                *error = QStringLiteral("USB 遥测杆 %1 保留位无效。")
                    .arg(rod + 1);
            }
            return false;
        }
        target.referenceT0Sample = readF64(payload, offset + 16);
        target.currentT0Sample = readF64(payload, offset + 24);
        target.nccPeak = readF64(payload, offset + 32);
        target.nccPeakRatio = readF64(payload, offset + 40);
        target.nccLagSamples = readF64(payload, offset + 48);
        target.delayNs = readF64(payload, offset + 56);
        target.snrDb = readF64(payload, offset + 64);
        target.forceN = readF64(payload, offset + 72);
        if ((decoded.forceAvailableMask & (1U << rod)) != 0U
            && !std::isfinite(target.forceN)) {
            if (error != nullptr) {
                *error = QStringLiteral("杆 %1 力值不是有限数。").arg(rod + 1);
            }
            return false;
        }
    }
    if (decoded.formalForceValid
        && (!std::isfinite(decoded.formalTotalN)
            || !std::isfinite(decoded.imbalanceIndex))) {
        if (error != nullptr) {
            *error = QStringLiteral("正式总力或不平衡值不是有限数。");
        }
        return false;
    }
    *snapshot = decoded;
    return true;
}

bool decodeUsbLogListV1(const QByteArray &payload,
                        DeviceLogListResult *result, QString *error)
{
    if (result == nullptr || payload.size() != 272
        || readU32(payload, 0) != kLogListToken
        || readU16(payload, 4) != kUsbWireAbiV1
        || readU16(payload, 6) != 272
        || readU32(payload, 8) != 4U
        || readU32(payload, 12) != 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 日志列表对象无效。");
        }
        return false;
    }
    DeviceLogListResult decoded;
    decoded.success = true;
    decoded.message = QStringLiteral("四个固定ARM日志源已读取。");
    for (quint32 index = 0; index < 4U; ++index) {
        const int offset = 16 + static_cast<int>(index) * 64;
        const quint32 sourceId = readU32(payload, offset);
        const quint32 flags = readU32(payload, offset + 4);
        const quint64 snapshotId = readU64(payload, offset + 24);
        QByteArray rawName = payload.mid(offset + 40, 24);
        const int terminator = rawName.indexOf('\0');
        if (terminator >= 0) {
            rawName.truncate(terminator);
        }
        if (sourceId != index + 1U || (flags & ~0x00000001U) != 0U
            || readU32(payload, offset + 36) != 0U || rawName.isEmpty()
            || ((flags & 1U) != 0U && snapshotId == 0U)) {
            if (error != nullptr) {
                *error = QStringLiteral("USB 日志源 %1 元数据无效。")
                    .arg(index + 1U);
            }
            return false;
        }
        DeviceLogSource source;
        source.sourceId = sourceId;
        source.available = (flags & 1U) != 0U;
        source.totalBytes = readU64(payload, offset + 8);
        source.modifiedTimeNs = readU64(payload, offset + 16);
        source.snapshotId = snapshotId;
        source.fileIdentityCrc32 = readU32(payload, offset + 32);
        source.name = QString::fromLatin1(rawName);
        decoded.sources.push_back(source);
    }
    *result = decoded;
    return true;
}

bool decodeUsbLogChunkV1(const QByteArray &payload, quint32 sourceId,
                         quint64 offset, quint64 snapshotId,
                         DeviceLogChunkResult *result, QString *error)
{
    if (result == nullptr || payload.size() < 40
        || readU32(payload, 0) != sourceId
        || (readU32(payload, 4) & ~0x00000001U) != 0U
        || readU64(payload, 8) != offset
        || readU64(payload, 32) != snapshotId
        || readU32(payload, 24) !=
            static_cast<quint32>(payload.size() - 40)) {
        if (error != nullptr) {
            *error = QStringLiteral("USB 日志分块回执与请求不一致。");
        }
        return false;
    }
    DeviceLogChunkResult decoded;
    decoded.success = true;
    decoded.message = QStringLiteral("ARM日志分块读取成功。");
    decoded.sourceId = sourceId;
    decoded.offset = offset;
    decoded.totalBytes = readU64(payload, 16);
    decoded.fileIdentityCrc32 = readU32(payload, 28);
    decoded.snapshotId = snapshotId;
    decoded.more = (readU32(payload, 4) & 1U) != 0U;
    decoded.data = payload.mid(40);
    *result = decoded;
    return true;
}

} // namespace ucm
