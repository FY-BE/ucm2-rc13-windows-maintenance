#pragma once

#include "config_transport.h"

#include <QStringList>

namespace ucm {

class UsbBurstHilPort {
public:
    virtual ~UsbBurstHilPort() = default;
    virtual ControlAuthorityResultV2 readAuthority() = 0;
    virtual ControlAuthorityResultV2 switchMode(quint32 mode) = 0;
    virtual ControlAuthorityResultV2 renewLease() = 0;
    virtual UsbRuntimeConfigOperationResultV9 readConfig(
        quint32 kind, quint64 transactionId = 0U) = 0;
    virtual UsbRuntimeConfigOperationResultV9 submitConfig(
        const UsbRuntimeConfigObjectV9 &object) = 0;
    virtual quint64 nextTransactionId() = 0;
    virtual void waitForTerminalPoll() = 0;
};

struct UsbBurstHilOptions {
    quint32 expectedOriginalBurst = 1U;
    quint32 temporaryBurst = 2U;
    quint16 burstGroupId = 0U;
    int maximumTerminalPolls = 50;
};

struct UsbBurstHilReport {
    bool passed = false;
    bool temporaryApplied = false;
    bool originalRestored = false;
    bool startupSaved = false;
    bool autonomousRestored = false;
    quint32 observedOriginalBurst = 0U;
    quint32 observedTemporaryBurst = 0U;
    QString failure;
    QStringList steps;
};

UsbBurstHilReport runUsbBurstHilWorkflow(
    UsbBurstHilPort &port, const UsbBurstHilOptions &options);

} // namespace ucm
