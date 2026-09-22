#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ucm {

struct DiagnosticPackageFile {
    QString name;
    QByteArray data;
};

struct DiagnosticPackageResult {
    bool success = false;
    QString message;
    QString packagePath;
};

DiagnosticPackageResult writeDiagnosticPackage(
    const QString &baseDirectory, const QString &packageName,
    const QJsonObject &manifest,
    const QVector<DiagnosticPackageFile> &files);

} // namespace ucm
