#include "upgrade_package_check.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryFile>
#include <QTextStream>
#include <QtEndian>

namespace {
void p16(QByteArray &bytes, int offset, quint16 value)
{
    qToLittleEndian(value, bytes.data() + offset);
}
void p32(QByteArray &bytes, int offset, quint32 value)
{
    qToLittleEndian(value, bytes.data() + offset);
}
void p64(QByteArray &bytes, int offset, quint64 value)
{
    qToLittleEndian(value, bytes.data() + offset);
}
QByteArray sha(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
void seal(QByteArray &bytes)
{
    p32(bytes, 508, 0);
    quint32 crc = ~quint32(0);
    for (unsigned char value : bytes.left(512)) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    p32(bytes, 508, ~crc);
}
QByteArray package()
{
    QByteArray bytes(1536, 0);
    QCryptographicHash system(QCryptographicHash::Sha256);
    for (int index = 0; index < 6; ++index) {
        const qsizetype aligned = (bytes.size() + 511) & ~qsizetype(511);
        bytes.append(QByteArray(aligned - bytes.size(), 0));
        const QByteArray data(index + 17, char('A' + index));
        const int entryOffset = 512 + index * 128;
        const quint32 id = quint32(index + 1);
        const quint32 mode = index < 5 ? 0755 : 0644;
        p32(bytes, entryOffset, id);
        p32(bytes, entryOffset + 4, mode);
        p64(bytes, entryOffset + 8, quint64(bytes.size()));
        p64(bytes, entryOffset + 16, quint64(data.size()));
        bytes.replace(entryOffset + 24, 32, sha(data));
        QByteArray record(48, 0);
        p32(record, 0, id);
        p32(record, 4, mode);
        p64(record, 8, quint64(data.size()));
        record.replace(16, 32, sha(data));
        system.addData(record);
        bytes.append(data);
    }
    p32(bytes, 0, 0x31503255U); // U2P1
    p16(bytes, 4, 1);
    p16(bytes, 6, 512);
    p32(bytes, 8, 156);
    p32(bytes, 12, 6);
    p64(bytes, 16, quint64(bytes.size()));
    p64(bytes, 24, 512);
    p64(bytes, 32, 1536);
    bytes.replace(40, 32, system.result());
    bytes.replace(72, 4, "A001");
    bytes.replace(104, 11, "test-target");
    bytes.replace(128, 11, "test-active");
    seal(bytes);
    return bytes;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool valid, const char *name) {
        QTextStream(stdout) << (valid ? "PASS " : "FAIL ") << name << '\n';
        if (!valid) ++failures;
    };
    auto run = [&](const QByteArray &bytes, quint32 version = 156,
                   const QByteArray &model = "A001",
                   const QByteArray &target = "test-target",
                   const QByteArray &active = "test-active") {
        QTemporaryFile file;
        if (!file.open() || file.write(bytes) != bytes.size() || !file.flush() ||
            !file.seek(11)) return false;
        QByteArray wholeSha("stale");
        QString error;
        const bool valid = checkUpgradePackage(file, version, model, target,
                                               active, &wholeSha, &error);
        check(file.pos() == 11, "file position restored");
        check(valid ? wholeSha == sha(bytes) : wholeSha.isEmpty(),
              "digest success/failure contract");
        return valid;
    };
    const QByteArray good = package();
    {
        QTemporaryFile file;
        UpgradePackageMetadata metadata;
        QString error;
        const bool ready = file.open() && file.write(good) == good.size() &&
            file.flush() && file.seek(19);
        const bool valid = ready &&
            readUpgradePackageMetadata(file, &metadata, &error);
        check(valid, "package metadata parsed");
        check(file.pos() == 19, "metadata read restores file position");
        check(metadata.packageVersion == 156 && metadata.deviceModel == "A001" &&
              metadata.targetBuildId == "test-target" &&
              metadata.requiredBuildId == "test-active",
              "package metadata identities match header");
    }
    check(run(good), "valid unsigned R2S U2P1 package");
    check(!run(good, 157), "version mismatch rejected");
    check(!run(good, 156, "DE168"), "model mismatch rejected");
    check(!run(good, 156, "A001", "wrong-target"), "target build mismatch rejected");
    check(!run(good, 156, "A001", "test-target", "wrong-active"),
          "required active build mismatch rejected");
    check(!run(good, 156, "A001", "test-target", "test-target"),
          "same active and target build rejected");
    QByteArray bad = good;
    p32(bad, 0, 0x31504155U); seal(bad);
    check(!run(bad), "obsolete UAP1 rejected");
    bad = good; bad[508] ^= 1;
    check(!run(bad), "header CRC rejected");
    bad = good; bad[512 + 4] ^= 1;
    check(!run(bad), "artifact mode rejected");
    bad = good; bad[512 + 24] ^= 1;
    check(!run(bad), "artifact digest rejected");
    bad = good; bad[512 + 56] = 1;
    check(!run(bad), "artifact reserved bytes rejected");
    bad = good; bad[40] ^= 1; seal(bad);
    check(!run(bad), "system identity rejected");
    bad = good; bad[bad.size() - 1] ^= 1;
    check(!run(bad), "payload corruption rejected");
    bad = good; bad.chop(1);
    check(!run(bad), "truncated package rejected");

    const QString realPath = qEnvironmentVariable("UCM_R2S_REAL_U2P1");
    if (!realPath.isEmpty()) {
        QFile real(realPath);
        QByteArray wholeSha;
        QString error;
        const bool opened = real.open(QIODevice::ReadOnly);
        const bool valid = opened && checkUpgradePackage(real, 156, "A001",
            "UCM2-R2S-20260913-156", "UCM2-R2S-20260913-143",
            &wholeSha, &error);
        check(valid && wholeSha.toHex() ==
              "aa95085da550840778acd4123a2bab0062a41410a3f1e5fd070c5c30cad75d6e",
              "actual ARM156 U2P1 package accepted");
        if (!valid) QTextStream(stderr) << error << '\n';
    }
    QTextStream(stdout) << "upgrade_package_check failures=" << failures << '\n';
    return failures ? 1 : 0;
}
