#pragma once

#include "config_transport.h"

#include <QString>

namespace ucm {

enum class RuntimeDisplayState {
    Simulation,
    UsbOffline,
    ReceiverUnavailable,
    RuntimeUnavailable,
    Starting,
    CapturingNoSnapshot,
    CapturingDiagnostic,
    FormalReady,
    SafeWait,
    Reconfiguring,
    Stopping
};

struct RuntimeStatusPresentation {
    RuntimeDisplayState state = RuntimeDisplayState::UsbOffline;
    bool usbOnline = false;
    bool receiverReady = false;
    bool daemonReady = false;
    bool measurementSnapshotAvailable = false;
    bool formalValid = false;
    QString linkText;
    QString measurementText;
    QString detailText;
};

RuntimeStatusPresentation presentRuntimeStatus(
    const TransportInfo &transport,
    const RuntimeStatusResultV2 &runtime);

} // namespace ucm
