#include "runtime_status_model.h"
#include "usb_extended_wire_v2.h"

#include <QCoreApplication>
#include <QTextStream>

using namespace ucm;

namespace {

int failures = 0;

void expect(bool condition, const QString &name)
{
    QTextStream(stdout) << (condition ? "PASS  " : "FAIL  ")
                        << name << '\n';
    if (!condition) ++failures;
}

TransportInfo onlineTransport()
{
    TransportInfo transport;
    transport.simulationOnly = false;
    transport.realUsbOpened = true;
    transport.armReceiverContacted = true;
    transport.scope = QStringLiteral("full_whitelisted_configuration");
    return transport;
}

RuntimeStatusResultV2 activeRuntime(quint32 canonicalPhase,
                                    bool formalValid)
{
    RuntimeStatusResultV2 result;
    result.success = true;
    result.status.available = true;
    result.status.flags = kUsbRuntimeStatusDaemonRunningV2
        | kUsbRuntimeStatusCaptureLoopActiveV2
        | kUsbRuntimeStatusHardwareActiveV2;
    if (canonicalPhase != kUsbRuntimeCanonicalNoneV2) {
        result.status.flags |= kUsbRuntimeStatusLastCanonicalAvailableV2;
    }
    if (formalValid) {
        result.status.flags |= kUsbRuntimeStatusFormalValidV2
            | kUsbRuntimeStatusTemplateActiveV2;
    }
    result.status.runState = kUsbRuntimeActiveV2;
    result.status.controlMode = kUsbControlModeAutonomousV2;
    result.status.controlPhase = kUsbControlPhaseAutonomousTrackingV2;
    result.status.templateState = formalValid
        ? kUsbRuntimeTemplateActiveV2 : kUsbRuntimeTemplateBuildingV2;
    result.status.canonicalPhase = canonicalPhase;
    result.status.heartbeatSequence = 42U;
    result.status.activeSessionId = 9U;
    return result;
}

void linkAndSnapshotStatesStayIndependent()
{
    TransportInfo transport = onlineTransport();
    RuntimeStatusResultV2 unavailable;
    unavailable.message = QStringLiteral("status=7 / NOT_FOUND");
    RuntimeStatusPresentation view = presentRuntimeStatus(
        transport, unavailable);
    expect(view.state == RuntimeDisplayState::RuntimeUnavailable
           && view.usbOnline && view.receiverReady && !view.daemonReady
           && view.linkText == QStringLiteral("USB ONLINE")
           && view.measurementText.contains(QStringLiteral("运行状态不可用")),
        QStringLiteral(
            "missing runtime object does not relabel an online USB receiver as offline"));

    view = presentRuntimeStatus(
        transport, activeRuntime(kUsbRuntimeCanonicalNoneV2, false));
    expect(view.state == RuntimeDisplayState::CapturingNoSnapshot
           && view.usbOnline && view.receiverReady && view.daemonReady
           && !view.measurementSnapshotAvailable && !view.formalValid
           && view.measurementText.contains(QStringLiteral("尚无快照")),
        QStringLiteral(
            "active capture without echo snapshot remains online and explicitly pending"));
}

void safeWaitAndFormalStatesAreExplicit()
{
    TransportInfo transport = onlineTransport();
    RuntimeStatusResultV2 safeWait;
    safeWait.success = true;
    safeWait.status.available = true;
    safeWait.status.flags = kUsbRuntimeStatusDaemonRunningV2
        | kUsbRuntimeStatusSafeWaitV2;
    safeWait.status.runState = kUsbRuntimeSafeConfigurationWaitV2;
    safeWait.status.controlMode = kUsbControlModeHostManagedV2;
    safeWait.status.controlPhase = kUsbControlPhaseHostSafeWaitV2;
    safeWait.status.templateState = kUsbRuntimeTemplateNoneV2;
    safeWait.status.heartbeatSequence = 8U;
    safeWait.status.activeSessionId = 5U;
    RuntimeStatusPresentation view = presentRuntimeStatus(
        transport, safeWait);
    expect(view.state == RuntimeDisplayState::SafeWait
           && view.daemonReady && !view.measurementSnapshotAvailable
           && view.measurementText.contains(QStringLiteral("激励已关闭")),
        QStringLiteral("safe-wait is not reported as a USB disconnection"));

    view = presentRuntimeStatus(
        transport, activeRuntime(kUsbRuntimeCanonicalR3CompleteV2, true));
    expect(view.state == RuntimeDisplayState::FormalReady
           && view.formalValid && view.measurementSnapshotAvailable
           && view.measurementText.contains(QStringLiteral("正式测量有效")),
        QStringLiteral("formal-ready requires an explicit runtime formal flag"));
}

void physicalOfflineRemainsDistinct()
{
    TransportInfo transport = onlineTransport();
    transport.realUsbOpened = false;
    transport.armReceiverContacted = false;
    const RuntimeStatusPresentation view = presentRuntimeStatus(
        transport, {});
    expect(view.state == RuntimeDisplayState::UsbOffline
           && !view.usbOnline && !view.receiverReady
           && view.linkText == QStringLiteral("USB OFFLINE"),
        QStringLiteral("physical USB absence remains the only USB-offline state"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    linkAndSnapshotStatesStayIndependent();
    safeWaitAndFormalStatesAreExplicit();
    physicalOfflineRemainsDistinct();
    return failures == 0 ? 0 : 1;
}
