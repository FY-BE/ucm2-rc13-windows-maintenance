#include "runtime_status_model.h"

#include "usb_extended_wire_v2.h"

namespace ucm {

RuntimeStatusPresentation presentRuntimeStatus(
    const TransportInfo &transport,
    const RuntimeStatusResultV2 &runtime)
{
    RuntimeStatusPresentation result;
    if (transport.simulationOnly) {
        result.state = RuntimeDisplayState::Simulation;
        result.linkText = QStringLiteral("MOCK");
        result.measurementText = QStringLiteral("离线模拟");
        result.detailText = QStringLiteral("未连接真实USB或ARM。");
        return result;
    }
    if (!transport.realUsbOpened) {
        result.state = RuntimeDisplayState::UsbOffline;
        result.linkText = QStringLiteral("USB OFFLINE");
        result.measurementText = QStringLiteral("WinUSB接口未打开");
        result.detailText = QStringLiteral("尚未建立物理USB传输。");
        return result;
    }
    result.usbOnline = true;
    if (!transport.armReceiverContacted) {
        result.state = RuntimeDisplayState::ReceiverUnavailable;
        result.linkText = QStringLiteral("USB OPEN");
        result.measurementText = QStringLiteral("ARM receiver握手未闭合");
        result.detailText = QStringLiteral(
            "物理USB存在，但能力、配置或控制权合同未闭合。");
        return result;
    }
    result.receiverReady = true;
    result.linkText = QStringLiteral("USB ONLINE");
    if (!runtime.success || !runtime.status.available) {
        result.state = RuntimeDisplayState::RuntimeUnavailable;
        result.measurementText = QStringLiteral("ARM运行状态不可用");
        result.detailText = runtime.message.isEmpty()
            ? QStringLiteral("receiver在线，但尚未取得measurementd状态对象。")
            : runtime.message;
        return result;
    }

    const RuntimeStatusV2 &status = runtime.status;
    result.daemonReady = true;
    result.measurementSnapshotAvailable =
        (status.flags & kUsbRuntimeStatusLastCanonicalAvailableV2) != 0U;
    result.formalValid =
        (status.flags & kUsbRuntimeStatusFormalValidV2) != 0U;

    switch (status.runState) {
    case kUsbRuntimeStartingV2:
        result.state = RuntimeDisplayState::Starting;
        result.measurementText = QStringLiteral("ARM测量进程启动中");
        break;
    case kUsbRuntimeActiveV2:
        if (result.formalValid) {
            result.state = RuntimeDisplayState::FormalReady;
            result.measurementText = QStringLiteral("50 Hz正式测量有效");
        } else if (result.measurementSnapshotAvailable) {
            result.state = RuntimeDisplayState::CapturingDiagnostic;
            result.measurementText = QStringLiteral(
                "50 Hz真实采集中 · 仅诊断/建模");
        } else {
            result.state = RuntimeDisplayState::CapturingNoSnapshot;
            result.measurementText = QStringLiteral(
                "50 Hz真实采集中 · 尚无快照/回波");
        }
        break;
    case kUsbRuntimeSafeConfigurationWaitV2:
        result.state = RuntimeDisplayState::SafeWait;
        result.measurementText = QStringLiteral("配置安全等待 · 激励已关闭");
        break;
    case kUsbRuntimeReconfiguringV2:
        result.state = RuntimeDisplayState::Reconfiguring;
        result.measurementText = QStringLiteral("配置重整中");
        break;
    case kUsbRuntimeStoppingV2:
        result.state = RuntimeDisplayState::Stopping;
        result.measurementText = QStringLiteral("ARM测量进程停止中");
        break;
    default:
        result.state = RuntimeDisplayState::RuntimeUnavailable;
        result.daemonReady = false;
        result.measurementText = QStringLiteral("ARM运行状态字段未知");
        break;
    }

    result.detailText = QStringLiteral(
        "%1 · %2 · 模板%3 · heartbeat=%4 · session=%5 · fault=%6/%7")
        .arg(usbControlModeTextV2(status.controlMode),
             usbControlPhaseTextV2(status.controlPhase),
             usbRuntimeTemplateStateTextV2(status.templateState))
        .arg(status.heartbeatSequence)
        .arg(status.activeSessionId)
        .arg(usbRuntimeFaultDomainTextV2(status.lastFaultDomain))
        .arg(status.lastFaultCode);
    return result;
}

} // namespace ucm
