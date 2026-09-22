#include "ethercat_port_probe.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const EthercatPortProbeResult result = EthercatPortScanner::scan();
    QVariantMap report = result.evidence;
    report.insert(QStringLiteral("success"), result.success);
    report.insert(QStringLiteral("detected"), result.detected);
    report.insert(QStringLiteral("status"), result.statusText);
    QTextStream(stdout) << QJsonDocument::fromVariant(report)
                               .toJson(QJsonDocument::Indented);
    return result.detected ? 0 : 13;
}
