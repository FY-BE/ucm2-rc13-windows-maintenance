#include "config_session.h"
#include "usb_burst_hil_workflow.h"
#include "usb_extended_wire_v2.h"
#include "usb_functionfs_transport.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTextStream>
#include <QThread>

#include <memory>

namespace {

class RealUsbPort final : public ucm::UsbBurstHilPort {
public:
    RealUsbPort()
        : m_session(std::make_unique<ucm::UsbFunctionfsTransport>()) {}

    ucm::TransportInfo info() const { return m_session.transportInfo(); }
    ucm::UsbExtendedDiscoveryV2 discovery() const
    { return m_session.usbExtendedDiscovery(); }
    QJsonObject evidence() const { return m_session.evidence(); }

    ucm::ControlAuthorityResultV2 readAuthority() override
    { return m_session.readControlAuthorityState(); }
    ucm::ControlAuthorityResultV2 switchMode(quint32 mode) override
    { return m_session.switchControlMode(mode); }
    ucm::ControlAuthorityResultV2 renewLease() override
    { return m_session.renewControlLease(); }
    ucm::UsbRuntimeConfigOperationResultV9 readConfig(
        quint32 kind, quint64 transactionId) override
    { return m_session.readRuntimeConfigV9(kind, transactionId); }
    ucm::UsbRuntimeConfigOperationResultV9 submitConfig(
        const ucm::UsbRuntimeConfigObjectV9 &object) override
    { return m_session.submitRuntimeConfigV9(object); }
    quint64 nextTransactionId() override
    {
        quint64 id = QRandomGenerator::global()->generate64();
        return id == 0U ? 1U : id;
    }
    void waitForTerminalPoll() override { QThread::msleep(100U); }

private:
    ucm::ConfigurationSession m_session;
};

bool option(const QStringList &arguments, const QString &name, int *value)
{
    const int at = arguments.indexOf(name);
    if (at < 0 || at + 1 >= arguments.size()) return false;
    bool ok = false;
    const int parsed = arguments.at(at + 1).toInt(&ok);
    if (!ok) return false;
    *value = parsed;
    return true;
}

QJsonObject reportObject(const ucm::UsbBurstHilReport &report)
{
    QJsonArray steps;
    for (const QString &step : report.steps) steps.push_back(step);
    return {
        {QStringLiteral("passed"), report.passed},
        {QStringLiteral("temporary_applied"), report.temporaryApplied},
        {QStringLiteral("original_restored"), report.originalRestored},
        {QStringLiteral("startup_saved"), report.startupSaved},
        {QStringLiteral("autonomous_restored"), report.autonomousRestored},
        {QStringLiteral("observed_original_burst"),
         static_cast<int>(report.observedOriginalBurst)},
        {QStringLiteral("observed_temporary_burst"),
         static_cast<int>(report.observedTemporaryBurst)},
        {QStringLiteral("failure"), report.failure},
        {QStringLiteral("steps"), steps}
    };
}

void print(const QJsonObject &object)
{
    QTextStream(stdout)
        << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    int expected = 0;
    int temporary = 0;
    QJsonObject output {
        {QStringLiteral("tool"), QStringLiteral("UCM_USB_BURST_HIL_R344")},
        {QStringLiteral("wire_revision"), 9},
        {QStringLiteral("requested_write"), false}
    };
    if (!arguments.contains(QStringLiteral("--execute-real-usb"))
        || !option(arguments, QStringLiteral("--expected-original-burst"),
                   &expected)
        || !option(arguments, QStringLiteral("--temporary-burst"),
                   &temporary)) {
        output.insert(QStringLiteral("error"), QStringLiteral(
            "No USB was opened. Required: --execute-real-usb "
            "--expected-original-burst 1 --temporary-burst 2"));
        print(output);
        return 2;
    }
    output.insert(QStringLiteral("requested_write"), true);
    if (expected < 1 || expected > 8 || temporary < 1 || temporary > 8
        || expected == temporary) {
        output.insert(QStringLiteral("error"),
                      QStringLiteral("burst参数必须为1–8且原值与临时值不同；USB未打开。"));
        print(output);
        return 2;
    }

    RealUsbPort port;
    const ucm::TransportInfo transport = port.info();
    output.insert(QStringLiteral("real_usb_opened"), transport.realUsbOpened);
    output.insert(QStringLiteral("arm_receiver_contacted"),
                  transport.armReceiverContacted);
    if (!transport.realUsbOpened || !transport.armReceiverContacted) {
        output.insert(QStringLiteral("error"),
                      QStringLiteral("真实USB revision 9握手失败。"));
        output.insert(QStringLiteral("evidence"), port.evidence());
        print(output);
        return 3;
    }

    const ucm::UsbExtendedDiscoveryV2 discovery = port.discovery();
    quint16 burstGroupId = 0U;
    for (const auto &parameter : discovery.parameters) {
        if (parameter.fieldId == ucm::UsbParameterTxBurstCyclesV2)
            burstGroupId = parameter.groupId;
    }
    if (!discovery.catalogReady || burstGroupId == 0U) {
        output.insert(QStringLiteral("error"),
                      QStringLiteral("field 98参数目录未闭合。"));
        output.insert(QStringLiteral("evidence"), port.evidence());
        print(output);
        return 4;
    }

    ucm::UsbBurstHilOptions options;
    options.expectedOriginalBurst = static_cast<quint32>(expected);
    options.temporaryBurst = static_cast<quint32>(temporary);
    options.burstGroupId = burstGroupId;
    const ucm::UsbBurstHilReport report =
        ucm::runUsbBurstHilWorkflow(port, options);
    output.insert(QStringLiteral("report"), reportObject(report));
    output.insert(QStringLiteral("evidence"), port.evidence());
    print(output);
    return report.passed ? 0 : 5;
}
