#include "usb_burst_hil_workflow.h"
#include "usb_extended_wire_v2.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool value, const char *message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FakePort final : public ucm::UsbBurstHilPort {
public:
    FakePort()
    {
        authority.available = true;
        authority.appliedMode = ucm::kUsbControlModeAutonomousV2;
        authority.requestedMode = ucm::kUsbControlModeAutonomousV2;
        authority.phase = ucm::kUsbControlPhaseAutonomousTrackingV2;
        authority.owner = ucm::kUsbControlOwnerM509V2;
        authority.publishedMonotonicNs = 100;
        authority.hostLeaseDeadlineNs = 1000;
        active.activeGeneration = 7;
        active.activeConfiguration.txBurstCycles = 1;
        active.activeConfiguration.presentGroupMask = 0x7ff;
        active.actualTxBurstCycles = 1;
        active.hardwareFlags = ucm::kUsbRuntimeHardwareStateValidV9;
        startup = active;
    }

    ucm::ControlAuthorityResultV2 readAuthority() override
    { return {true, {}, authority, 0}; }
    ucm::ControlAuthorityResultV2 switchMode(quint32 mode) override
    {
        authority.appliedMode = mode;
        authority.requestedMode = mode;
        authority.phase = mode == ucm::kUsbControlModeAutonomousV2
            ? ucm::kUsbControlPhaseAutonomousTrackingV2
            : ucm::kUsbControlPhaseHostActiveV2;
        authority.owner = mode == ucm::kUsbControlModeAutonomousV2
            ? ucm::kUsbControlOwnerM509V2 : ucm::kUsbControlOwnerHostV2;
        authority.hardwareActive = false;
        authority.transactionId = 0;
        return {true, {}, authority, ++transaction};
    }
    ucm::ControlAuthorityResultV2 renewLease() override
    { return {true, {}, authority, ++transaction}; }
    ucm::UsbRuntimeConfigOperationResultV9 readConfig(
        quint32 kind, quint64 = 0U) override
    {
        ucm::UsbRuntimeConfigOperationResultV9 result;
        result.success = true;
        result.receipt = kind == ucm::UsbRuntimeConfigQueryStartupV9
            ? startup : active;
        result.receipt.kind = kind == ucm::UsbRuntimeConfigQueryTransactionV9
                && pendingTerminal
            ? pendingTerminalKind : ucm::UsbRuntimeConfigReceiptActiveV9;
        result.receipt.result = 0;
        if (kind == ucm::UsbRuntimeConfigQueryTransactionV9)
            pendingTerminal = false;
        return result;
    }
    ucm::UsbRuntimeConfigOperationResultV9 submitConfig(
        const ucm::UsbRuntimeConfigObjectV9 &object) override
    {
        ++submitCount;
        operations.push_back(object.operation);
        submittedBursts.push_back(object.txBurstCycles);
        if (object.operation == ucm::UsbRuntimeConfigApplyV9) {
            active.activeGeneration = object.candidateGeneration;
            active.activeConfiguration = object;
            active.actualTxBurstCycles = object.txBurstCycles;
            active.hardwareFlags = ucm::kUsbRuntimeHardwareStateValidV9;
            const bool injectedFailure = failTemporaryAfterWrite
                && object.txBurstCycles == 2U;
            if (injectedFailure)
                return {false, QStringLiteral("injected failure"), {}};
        }
        ucm::UsbRuntimeConfigOperationResultV9 result;
        result.success = true;
        result.receipt = active;
        result.receipt.transactionId = object.transactionId;
        result.receipt.operation = object.operation;
        result.receipt.result = 0;
        if (object.operation == ucm::UsbRuntimeConfigSaveStartupV9) {
            ++saveCount;
            if (failSave) return {false, QStringLiteral("save failure"), {}};
            startup = active;
            result.receipt.kind = ucm::UsbRuntimeConfigReceiptSavedStartupV9;
            result.receipt.persisted = true;
        } else {
            result.receipt.kind = ucm::UsbRuntimeConfigReceiptAppliedV9;
        }
        if (returnAcceptedOnce) {
            returnAcceptedOnce = false;
            pendingTerminal = true;
            pendingTerminalKind = result.receipt.kind;
            result.receipt.kind = ucm::UsbRuntimeConfigReceiptAcceptedV9;
            result.receipt.result = 11;
        }
        return result;
    }
    quint64 nextTransactionId() override { return ++transaction; }
    void waitForTerminalPoll() override {}

    ucm::ControlAuthorityStateV2 authority;
    ucm::UsbRuntimeConfigReceiptV9 active;
    ucm::UsbRuntimeConfigReceiptV9 startup;
    quint64 transaction = 100;
    int submitCount = 0;
    int saveCount = 0;
    bool failTemporaryAfterWrite = false;
    bool failSave = false;
    bool returnAcceptedOnce = false;
    bool pendingTerminal = false;
    quint32 pendingTerminalKind = 0;
    QVector<quint32> operations;
    QVector<quint32> submittedBursts;
};

ucm::UsbBurstHilOptions options()
{
    ucm::UsbBurstHilOptions value;
    value.expectedOriginalBurst = 1;
    value.temporaryBurst = 2;
    value.burstGroupId = 8;
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    FakePort success;
    const auto passed = ucm::runUsbBurstHilWorkflow(success, options());
    expect(passed.passed && passed.temporaryApplied
           && passed.originalRestored && passed.startupSaved
           && passed.autonomousRestored,
           "nominal workflow applies 1->2, restores/saves 1 and returns auto");
    expect(success.active.activeConfiguration.txBurstCycles == 1U
           && success.startup.activeConfiguration.txBurstCycles == 1U
           && success.saveCount == 1 && success.submitCount == 3,
           "nominal workflow performs exactly two Apply and one Save");
    expect(success.operations == QVector<quint32>({1U, 1U, 2U})
           && success.submittedBursts == QVector<quint32>({2U, 1U, 1U}),
           "wire operations preserve Apply2, Apply1, Save1 order");

    FakePort asynchronous;
    asynchronous.returnAcceptedOnce = true;
    const auto polled = ucm::runUsbBurstHilWorkflow(asynchronous, options());
    expect(polled.passed && !asynchronous.pendingTerminal,
           "ACCEPTED receipt is resolved by bounded transaction polling");

    FakePort uncertain;
    uncertain.failTemporaryAfterWrite = true;
    const auto recovered = ucm::runUsbBurstHilWorkflow(uncertain, options());
    expect(!recovered.passed && recovered.originalRestored
           && recovered.autonomousRestored
           && uncertain.active.activeConfiguration.txBurstCycles == 1U,
           "uncertain temporary response still restores original and auto");

    FakePort saveFailure;
    saveFailure.failSave = true;
    const auto unsaved = ucm::runUsbBurstHilWorkflow(saveFailure, options());
    expect(!unsaved.passed && unsaved.originalRestored
           && unsaved.autonomousRestored
           && saveFailure.active.activeConfiguration.txBurstCycles == 1U,
           "SaveStartup failure preserves restored original and returns auto");

    FakePort mismatch;
    mismatch.active.activeConfiguration.txBurstCycles = 3;
    mismatch.active.actualTxBurstCycles = 3;
    const auto rejected = ucm::runUsbBurstHilWorkflow(mismatch, options());
    expect(!rejected.passed && mismatch.submitCount == 0
           && rejected.autonomousRestored,
           "unexpected original burst aborts before write and returns auto");

    FakePort existingHost;
    existingHost.switchMode(ucm::kUsbControlModeHostManagedV2);
    const auto refusedHost = ucm::runUsbBurstHilWorkflow(
        existingHost, options());
    expect(!refusedHost.passed && existingHost.submitCount == 0
           && refusedHost.autonomousRestored,
           "pre-existing host session is never reused for HIL writes");

    std::cout << "PASS usb_burst_hil_workflow\n";
    return 0;
}
