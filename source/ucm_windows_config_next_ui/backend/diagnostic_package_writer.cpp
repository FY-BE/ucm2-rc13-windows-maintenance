#include "diagnostic_package_writer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace ucm {
namespace {

bool safeName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$"));
    return pattern.match(name).hasMatch()
        && name != QStringLiteral("manifest.json")
        && name != QStringLiteral("SHA256SUMS.txt")
        && name != QStringLiteral("PACKAGE_INCOMPLETE");
}

bool writeAtomic(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(data) == data.size() && file.commit();
}

QByteArray digest(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256)
        .toHex();
}

} // namespace

DiagnosticPackageResult writeDiagnosticPackage(
    const QString &baseDirectory, const QString &packageName,
    const QJsonObject &manifest,
    const QVector<DiagnosticPackageFile> &files)
{
    QDir base(baseDirectory);
    if (!base.exists() || !safeName(packageName)) {
        return {false, QStringLiteral("诊断包目录或名称无效。"), {}};
    }
    QSet<QString> names;
    for (const DiagnosticPackageFile &file : files) {
        if (!safeName(file.name) || names.contains(file.name)) {
            return {false, QStringLiteral("诊断包文件名无效或重复：%1")
                .arg(file.name), {}};
        }
        names.insert(file.name);
    }
    if (!base.mkdir(packageName)) {
        return {false, QStringLiteral("诊断包目标已存在或无法创建。"), {}};
    }
    QDir package(base.absoluteFilePath(packageName));
    if (!writeAtomic(package.absoluteFilePath(
            QStringLiteral("PACKAGE_INCOMPLETE")), QByteArray("1\n"))) {
        return {false, QStringLiteral("无法创建诊断包完整性标记。"),
                package.absolutePath()};
    }

    QVector<DiagnosticPackageFile> ordered = files;
    std::sort(ordered.begin(), ordered.end(),
              [](const DiagnosticPackageFile &left,
                 const DiagnosticPackageFile &right) {
        return left.name < right.name;
    });
    QJsonArray fileManifest;
    QByteArray sums;
    for (const DiagnosticPackageFile &file : ordered) {
        const QByteArray sha = digest(file.data);
        if (!writeAtomic(package.absoluteFilePath(file.name), file.data)) {
            return {false, QStringLiteral("诊断文件写入失败：%1")
                .arg(file.name), package.absolutePath()};
        }
        fileManifest.append(QJsonObject {
            {QStringLiteral("name"), file.name},
            {QStringLiteral("bytes"), file.data.size()},
            {QStringLiteral("sha256"), QString::fromLatin1(sha)}
        });
        sums += sha + QByteArray("  ") + file.name.toUtf8() + '\n';
    }

    QJsonObject finalManifest = manifest;
    finalManifest.insert(QStringLiteral("files"), fileManifest);
    finalManifest.insert(QStringLiteral("file_count"), ordered.size());
    const QByteArray manifestBytes = QJsonDocument(finalManifest)
        .toJson(QJsonDocument::Indented);
    if (!writeAtomic(package.absoluteFilePath(QStringLiteral("manifest.json")),
                     manifestBytes)) {
        return {false, QStringLiteral("诊断包manifest写入失败。"),
                package.absolutePath()};
    }
    sums += digest(manifestBytes) + QByteArray("  manifest.json\n");
    if (!writeAtomic(package.absoluteFilePath(QStringLiteral("SHA256SUMS.txt")),
                     sums)) {
        return {false, QStringLiteral("诊断包SHA清单写入失败。"),
                package.absolutePath()};
    }
    if (!QFile::remove(package.absoluteFilePath(
            QStringLiteral("PACKAGE_INCOMPLETE")))) {
        return {false, QStringLiteral("诊断包完成标记清理失败。"),
                package.absolutePath()};
    }
    return {true, QStringLiteral("完整诊断包已生成。"),
            package.absolutePath()};
}

} // namespace ucm
