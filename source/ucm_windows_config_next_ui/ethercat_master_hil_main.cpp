#include "ethercat_master_session.h"
#include "fqx_machine_models.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        QTextStream(stderr)
            << "usage: UcmEthercatMasterHil <pcap-adapter> <mac> "
               "[cycles] [machine-model] [period-ms]\n";
        return 64;
    }
    ucm::ethercat::MasterRunOptions options;
    options.adapterId = args[1];
    options.sourceMac = QByteArray::fromHex(QString(args[2]).remove(':').remove('-').toLatin1());
    options.output.machineModel = 0x0025U;
    options.output.clampingForceSetpointKn = 1U;
    options.output.moldThickness = 6620U;
    options.output.machineState = 4U;
    if (args.size() > 3) {
        bool ok = false;
        const int cycles = args[3].toInt(&ok);
        if (!ok || cycles <= 0) return 64;
        options.cycles = cycles;
    }
    if (args.size() > 4) {
        bool ok = false;
        const unsigned int model = args[4].toUInt(&ok, 0);
        if (!ok || model > 0xffffU
            || !ucm::ethercat::isKnownFqxMachineModel(static_cast<std::uint16_t>(model))) {
            QTextStream(stderr) << "machine-model is not in the FQX protocol table\n";
            return 64;
        }
        options.output.machineModel = static_cast<quint16>(model);
    }
    if (args.size() > 5) {
        bool ok = false;
        const int periodMs = args[5].toInt(&ok);
        if (!ok || periodMs < 1 || periodMs > 1000) return 64;
        options.periodMs = periodMs;
    }
    const ucm::ethercat::MasterRunResult result = ucm::ethercat::MasterSession::run(options);
    QTextStream(stdout) << QJsonDocument(result.evidence).toJson(QJsonDocument::Indented);
    return result.success && result.returnedToInit ? 0 : 2;
}
