#pragma once
#include <QFile>
#include <QByteArray>
#include <QString>

struct UpgradePackageMetadata {
    quint32 packageVersion = 0;
    QByteArray deviceModel;
    QByteArray targetBuildId;
    QByteArray requiredBuildId;
};

// Reads and validates the immutable identity fields in the U2P1 header.
// Restores the original file position on both success and failure.
bool readUpgradePackageMetadata(QFile &file, UpgradePackageMetadata *metadata,
    QString *error);

// Local R2S U2P1 preflight. activeBuildId is read from the connected ARM.
// Restores file position and clears wholeSha on failure. ARM still performs
// the authoritative package, board identity and activation checks.
bool checkUpgradePackage(QFile &file, quint32 version, const QByteArray &model,
    const QByteArray &buildId, const QByteArray &activeBuildId,
    QByteArray *wholeSha, QString *error);
