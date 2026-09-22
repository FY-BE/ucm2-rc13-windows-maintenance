#include "upgrade_package_check.h"

#include <QCryptographicHash>
#include <QtEndian>
#include <algorithm>

namespace {
constexpr qint64 kHeaderBytes = 512;
constexpr qint64 kEntryBytes = 128;
constexpr int kArtifacts = 6;
constexpr qint64 kMaxPackageBytes = 16 * 1024 * 1024;

quint16 u16(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint16>(bytes.constData() + offset);
}
quint32 u32(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
quint64 u64(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint64>(bytes.constData() + offset);
}
void p32(QByteArray &bytes, int offset, quint32 value)
{
    qToLittleEndian(value, bytes.data() + offset);
}
void p64(QByteArray &bytes, int offset, quint64 value)
{
    qToLittleEndian(value, bytes.data() + offset);
}
bool zero(const QByteArray &bytes)
{
    for (char value : bytes)
        if (value != 0) return false;
    return true;
}
bool fixedIdentity(const QByteArray &slot, const QByteArray &expected)
{
    const int nul = slot.indexOf(char(0));
    return !expected.isEmpty() && nul == expected.size() &&
           slot.left(nul) == expected && zero(slot.mid(nul));
}
bool parseIdentity(const QByteArray &slot, QByteArray *identity)
{
    if (!identity) return false;
    identity->clear();
    const int nul = slot.indexOf(char(0));
    if (nul <= 0 || !zero(slot.mid(nul))) return false;
    const QByteArray value = slot.left(nul);
    for (unsigned char ch : value) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.';
        if (!allowed) return false;
    }
    *identity = value;
    return true;
}
quint32 crc32(QByteArray bytes)
{
    p32(bytes, 508, 0);
    quint32 crc = ~quint32(0);
    for (unsigned char value : bytes) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}
bool hashRange(QFile &file, qint64 offset, qint64 bytes, QByteArray *digest)
{
    if (!digest || offset < 0 || bytes < 0 || !file.seek(offset)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (bytes > 0) {
        const QByteArray block = file.read(std::min<qint64>(bytes, 16384));
        if (block.isEmpty()) return false;
        hash.addData(block);
        bytes -= block.size();
    }
    *digest = hash.result();
    return true;
}
quint64 align512(quint64 value)
{
    return (value + 511U) & ~quint64(511U);
}
}

bool readUpgradePackageMetadata(QFile &file, UpgradePackageMetadata *metadata,
    QString *error)
{
    if (error) error->clear();
    if (metadata) *metadata = UpgradePackageMetadata {};
    const qint64 originalPosition = file.pos();
    struct RestorePosition {
        QFile &file;
        qint64 position;
        ~RestorePosition() { if (position >= 0) file.seek(position); }
    } restore {file, originalPosition};
    auto reject = [&](const char *reason) {
        if (error) *error = QString::fromUtf8(reason);
        return false;
    };
    const qint64 size = file.size();
    if (!metadata || !file.isOpen() || !file.isReadable() ||
        file.isSequential() || size < kHeaderBytes + kArtifacts * kEntryBytes ||
        size > kMaxPackageBytes || !file.seek(0))
        return reject("升级包身份读取输入或文件大小无效。");

    const QByteArray header = file.read(kHeaderBytes);
    if (header.size() != kHeaderBytes || u32(header, 0) != 0x31503255U ||
        u16(header, 4) != 1 || u16(header, 6) != kHeaderBytes ||
        u32(header, 508) == 0 || u32(header, 508) != crc32(header))
        return reject("U2P1包头、ABI或CRC错误。");

    UpgradePackageMetadata parsed;
    parsed.packageVersion = u32(header, 8);
    const quint64 payloadOffset = u64(header, 32);
    if (parsed.packageVersion == 0 || u32(header, 12) != kArtifacts ||
        u64(header, 16) != quint64(size) || u64(header, 24) != kHeaderBytes ||
        payloadOffset < kHeaderBytes + kArtifacts * kEntryBytes ||
        payloadOffset > quint64(size) || payloadOffset % 512U != 0U ||
        zero(header.mid(40, 32)) || !zero(header.mid(152, 356)) ||
        !parseIdentity(header.mid(72, 32), &parsed.deviceModel) ||
        !parseIdentity(header.mid(104, 24), &parsed.targetBuildId) ||
        !parseIdentity(header.mid(128, 24), &parsed.requiredBuildId) ||
        (parsed.deviceModel != "A001" && parsed.deviceModel != "DE168" &&
         parsed.deviceModel != "GW1850R") ||
        parsed.targetBuildId == parsed.requiredBuildId)
        return reject("U2P1包版本、型号、目标/前置Build ID或布局无效。");

    *metadata = parsed;
    return true;
}

bool checkUpgradePackage(QFile &file, quint32 version, const QByteArray &model,
    const QByteArray &buildId, const QByteArray &activeBuildId,
    QByteArray *wholeSha, QString *error)
{
    if (error) error->clear();
    if (wholeSha) wholeSha->clear();
    const qint64 originalPosition = file.pos();
    struct RestorePosition {
        QFile &file;
        qint64 position;
        ~RestorePosition() { if (position >= 0) file.seek(position); }
    } restore {file, originalPosition};
    auto reject = [&](const char *reason) {
        if (error) *error = QString::fromUtf8(reason);
        return false;
    };
    const qint64 size = file.size();
    if (!wholeSha || !file.isOpen() || !file.isReadable() || file.isSequential() ||
        size < kHeaderBytes + kArtifacts * kEntryBytes ||
        size > kMaxPackageBytes || version == 0 ||
        (model != "A001" && model != "DE168" && model != "GW1850R") ||
        buildId.isEmpty() || buildId.size() >= 24 || buildId.contains(char(0)) ||
        activeBuildId.isEmpty() || activeBuildId.size() >= 24 ||
        activeBuildId.contains(char(0)) || buildId == activeBuildId ||
        !file.seek(0))
        return reject("升级预检输入、当前设备身份或文件大小无效。");

    const QByteArray header = file.read(kHeaderBytes);
    if (header.size() != kHeaderBytes || u32(header, 0) != 0x31503255U ||
        u16(header, 4) != 1 || u16(header, 6) != kHeaderBytes ||
        u32(header, 508) == 0 || u32(header, 508) != crc32(header))
        return reject("U2P1包头、ABI或CRC错误。");

    const quint64 payloadOffset = u64(header, 32);
    if (u32(header, 8) != version || u32(header, 12) != kArtifacts ||
        u64(header, 16) != quint64(size) || u64(header, 24) != kHeaderBytes ||
        payloadOffset < kHeaderBytes + kArtifacts * kEntryBytes ||
        payloadOffset > quint64(size) || payloadOffset % 512U != 0U ||
        zero(header.mid(40, 32)) || !zero(header.mid(152, 356)) ||
        !fixedIdentity(header.mid(72, 32), model) ||
        !fixedIdentity(header.mid(104, 24), buildId) ||
        !fixedIdentity(header.mid(128, 24), activeBuildId))
        return reject("包版本、型号、目标/当前Build ID或布局不匹配。");

    QCryptographicHash systemIdentity(QCryptographicHash::Sha256);
    quint64 nextOffset = payloadOffset;
    for (int index = 0; index < kArtifacts; ++index) {
        if (!file.seek(kHeaderBytes + index * kEntryBytes))
            return reject("无法定位U2P1文件表。");
        const QByteArray entry = file.read(kEntryBytes);
        if (entry.size() != kEntryBytes)
            return reject("U2P1文件表不完整。");
        const quint32 id = u32(entry, 0);
        const quint32 mode = u32(entry, 4);
        const quint64 offset = u64(entry, 8);
        const quint64 bytes = u64(entry, 16);
        const QByteArray expectedSha = entry.mid(24, 32);
        if (id != quint32(index + 1) ||
            mode != quint32(index < 5 ? 0755 : 0644) ||
            offset != nextOffset || bytes == 0 || offset > quint64(size) ||
            bytes > quint64(size) - offset || zero(expectedSha) ||
            !zero(entry.mid(56, 72)))
            return reject("U2P1文件顺序、权限、范围或保留字段错误。");
        QByteArray actualSha;
        if (!hashRange(file, qint64(offset), qint64(bytes), &actualSha) ||
            actualSha != expectedSha)
            return reject("U2P1文件摘要不匹配。");
        QByteArray record(48, 0);
        p32(record, 0, id);
        p32(record, 4, mode);
        p64(record, 8, bytes);
        record.replace(16, 32, expectedSha);
        systemIdentity.addData(record);
        nextOffset = align512(offset + bytes);
        if (index == kArtifacts - 1 && offset + bytes != quint64(size))
            return reject("U2P1末尾范围不完整。");
    }
    if (systemIdentity.result() != header.mid(40, 32))
        return reject("U2P1系统身份摘要不匹配。");
    QByteArray actualWhole;
    if (!hashRange(file, 0, size, &actualWhole) || file.size() != size ||
        !file.seek(0) || file.read(kHeaderBytes) != header)
        return reject("升级包读取期间发生变化。");
    *wholeSha = actualWhole;
    return true;
}
