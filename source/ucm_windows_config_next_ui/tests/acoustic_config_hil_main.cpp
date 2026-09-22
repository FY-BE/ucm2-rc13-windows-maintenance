#include "config_session.h"
#include "usb_extended_wire_v2.h"
#include "usb_functionfs_transport.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <memory>

namespace {

QJsonObject configurationObject(const ucm::Configuration &configuration)
{
    return {
        {QStringLiteral("generation"), configuration.cfgVersion},
        {QStringLiteral("rod_length_mm"),
         configuration.algorithm.rodLengthMm},
        {QStringLiteral("measurement_point_mm"),
         configuration.algorithm.measurementPointMm}
    };
}

QJsonObject authorityObject(const ucm::ControlAuthorityStateV2 &state)
{
    return {
        {QStringLiteral("available"), state.available},
        {QStringLiteral("applied_mode"),
         static_cast<int>(state.appliedMode)},
        {QStringLiteral("phase"), static_cast<int>(state.phase)},
        {QStringLiteral("hardware_active"), state.hardwareActive},
        {QStringLiteral("generation"), QString::number(state.generation)},
        {QStringLiteral("last_result"), state.lastResult}
    };
}

void printResult(const QJsonObject &result)
{
    QTextStream(stdout)
        << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
}

bool parseIntegerOption(const QStringList &arguments, const QString &name,
                        int *value)
{
    const int index = arguments.indexOf(name);
    if (index < 0 || index + 1 >= arguments.size()) return false;
    bool ok = false;
    const int parsed = arguments.at(index + 1).toInt(&ok);
    if (!ok) return false;
    *value = parsed;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int rodLengthMm = 0;
    int measurementPointMm = 0;
    const QStringList arguments = application.arguments();
    QJsonObject result {
        {QStringLiteral("tool"),
         QStringLiteral("UCM_USB_ACOUSTIC_CONFIG_HIL_V2")},
        {QStringLiteral("nonvolatile_write"), false}
    };
    if (!parseIntegerOption(arguments, QStringLiteral("--rod-length-mm"),
                            &rodLengthMm)
        || !parseIntegerOption(
            arguments, QStringLiteral("--measurement-point-mm"),
            &measurementPointMm)) {
        result.insert(QStringLiteral("error"),
                      QStringLiteral("required options: --rod-length-mm N "
                                     "--measurement-point-mm N"));
        printResult(result);
        return 2;
    }

    ucm::ConfigurationSession session(
        std::make_unique<ucm::UsbFunctionfsTransport>());
    const ucm::TransportInfo transport = session.transportInfo();
    result.insert(QStringLiteral("real_usb_opened"),
                  transport.realUsbOpened);
    result.insert(QStringLiteral("arm_receiver_contacted"),
                  transport.armReceiverContacted);
    if (!transport.armReceiverContacted) {
        result.insert(QStringLiteral("error"),
                      QStringLiteral("USB receiver handshake failed"));
        result.insert(QStringLiteral("evidence"), session.evidence());
        printResult(result);
        return 3;
    }

    result.insert(QStringLiteral("initial_configuration"),
                  configurationObject(session.active()));
    const ucm::ControlAuthorityResultV2 initialAuthority =
        session.readControlAuthorityState();
    result.insert(QStringLiteral("initial_authority"),
                  authorityObject(initialAuthority.state));
    if (!initialAuthority.success || !initialAuthority.state.available) {
        result.insert(QStringLiteral("error"), initialAuthority.message);
        printResult(result);
        return 4;
    }

    bool enteredHostMode = false;
    const auto restoreAutonomous = [&] {
        if (!enteredHostMode) return true;
        const ucm::ControlAuthorityResultV2 restored =
            session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
        result.insert(QStringLiteral("restored_authority"),
                      authorityObject(restored.state));
        enteredHostMode = false;
        return restored.success
            && restored.state.appliedMode
                == ucm::kUsbControlModeAutonomousV2;
    };

    ucm::ControlAuthorityResultV2 hostAuthority = initialAuthority;
    if (hostAuthority.state.appliedMode
        == ucm::kUsbControlModeAutonomousV2) {
        hostAuthority = session.switchControlMode(
            ucm::kUsbControlModeHostManagedV2);
        enteredHostMode = hostAuthority.success;
    } else if (hostAuthority.state.appliedMode
                   == ucm::kUsbControlModeHostManagedV2
               && hostAuthority.state.phase
                   == ucm::kUsbControlPhaseHostSafeWaitV2) {
        hostAuthority = session.resumeHostControl();
    }
    result.insert(QStringLiteral("host_authority"),
                  authorityObject(hostAuthority.state));
    if (!hostAuthority.success
        || hostAuthority.state.appliedMode
            != ucm::kUsbControlModeHostManagedV2
        || hostAuthority.state.phase
            != ucm::kUsbControlPhaseHostActiveV2) {
        result.insert(QStringLiteral("error"), hostAuthority.message);
        result.insert(QStringLiteral("restore_succeeded"),
                      restoreAutonomous());
        printResult(result);
        return 5;
    }

    ucm::Configuration candidate = session.active();
    candidate.algorithm.rodLengthMm = rodLengthMm;
    candidate.algorithm.measurementPointMm = measurementPointMm;
    session.setCandidate(candidate);
    if (!session.prepare() || !session.validateCandidate()
        || !session.commitRam() || !session.confirmReadback()) {
        result.insert(QStringLiteral("error"), session.lastMessage());
        result.insert(QStringLiteral("restore_succeeded"),
                      restoreAutonomous());
        result.insert(QStringLiteral("evidence"), session.evidence());
        printResult(result);
        return 6;
    }

    const ucm::Configuration applied = session.active();
    result.insert(QStringLiteral("applied_configuration"),
                  configurationObject(applied));
    const bool valuesMatch =
        applied.algorithm.rodLengthMm == rodLengthMm
        && applied.algorithm.measurementPointMm == measurementPointMm;
    const bool restored = restoreAutonomous();
    result.insert(QStringLiteral("restore_succeeded"), restored);
    result.insert(QStringLiteral("evidence"), session.evidence());
    const bool passed = valuesMatch && restored;
    result.insert(QStringLiteral("passed"), passed);
    if (!passed) {
        result.insert(QStringLiteral("error"),
                      QStringLiteral("applied values or final authority "
                                     "did not match"));
    }
    printResult(result);
    return passed ? 0 : 7;
}
