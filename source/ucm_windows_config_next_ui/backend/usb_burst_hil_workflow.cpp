#include "usb_burst_hil_workflow.h"

#include "usb_extended_wire_v2.h"

namespace ucm {
namespace {

bool terminalSuccess(const UsbRuntimeConfigOperationResultV9 &result,
                     quint32 operation)
{
    const quint32 expectedKind = operation == UsbRuntimeConfigApplyV9
        ? UsbRuntimeConfigReceiptAppliedV9
        : operation == UsbRuntimeConfigSaveStartupV9
            ? UsbRuntimeConfigReceiptSavedStartupV9
            : UsbRuntimeConfigReceiptValidatedV9;
    return result.success && result.receipt.kind == expectedKind
        && result.receipt.result == 0;
}

UsbRuntimeConfigOperationResultV9 submitAndResolve(
    UsbBurstHilPort &port, const UsbRuntimeConfigObjectV9 &object,
    int maximumPolls, QStringList *steps)
{
    UsbRuntimeConfigOperationResultV9 result = port.submitConfig(object);
    steps->push_back(QStringLiteral("submit operation=%1 tx=%2 kind=%3 success=%4")
        .arg(object.operation).arg(object.transactionId)
        .arg(result.receipt.kind).arg(result.success));
    for (int poll = 0;
         result.success
             && result.receipt.kind == UsbRuntimeConfigReceiptAcceptedV9
             && poll < maximumPolls;
         ++poll) {
        if ((poll % 4) == 3) {
            const ControlAuthorityResultV2 lease = port.renewLease();
            steps->push_back(QStringLiteral("renew lease success=%1")
                .arg(lease.success));
            if (!lease.success) return {false, lease.message, {}};
        }
        port.waitForTerminalPoll();
        result = port.readConfig(UsbRuntimeConfigQueryTransactionV9,
                                 object.transactionId);
        steps->push_back(QStringLiteral("poll tx=%1 kind=%2 success=%3")
            .arg(object.transactionId).arg(result.receipt.kind)
            .arg(result.success));
    }
    if (result.success
        && result.receipt.kind == UsbRuntimeConfigReceiptAcceptedV9) {
        result.success = false;
        result.message = QStringLiteral("等待配置终态超时。");
    }
    return result;
}

UsbRuntimeConfigObjectV9 makeApply(
    const UsbRuntimeConfigReceiptV9 &active, quint32 burst,
    quint16 burstGroupId, quint64 transactionId)
{
    UsbRuntimeConfigObjectV9 object = active.activeConfiguration;
    object.operation = UsbRuntimeConfigApplyV9;
    object.transactionId = transactionId;
    object.baseGeneration = active.activeGeneration;
    object.candidateGeneration = active.activeGeneration + 1U;
    object.changedGroupMask = 1ULL << (burstGroupId - 1U);
    object.txBurstCycles = burst;
    return object;
}

UsbRuntimeConfigObjectV9 makeSave(
    const UsbRuntimeConfigReceiptV9 &active, quint64 transactionId)
{
    UsbRuntimeConfigObjectV9 object = active.activeConfiguration;
    object.operation = UsbRuntimeConfigSaveStartupV9;
    object.transactionId = transactionId;
    object.baseGeneration = active.activeGeneration;
    object.candidateGeneration = active.activeGeneration;
    object.changedGroupMask = 0U;
    return object;
}

bool exactBurstReadback(const UsbRuntimeConfigOperationResultV9 &result,
                        quint32 expected)
{
    return result.success
        && result.receipt.activeConfiguration.txBurstCycles == expected
        && (result.receipt.hardwareFlags
            & kUsbRuntimeHardwareStateValidV9) != 0U
        && result.receipt.actualTxBurstCycles == expected;
}

} // namespace

UsbBurstHilReport runUsbBurstHilWorkflow(
    UsbBurstHilPort &port, const UsbBurstHilOptions &options)
{
    UsbBurstHilReport report;
    if (options.expectedOriginalBurst < 1U
        || options.expectedOriginalBurst > 8U
        || options.temporaryBurst < 1U || options.temporaryBurst > 8U
        || options.temporaryBurst == options.expectedOriginalBurst
        || options.burstGroupId == 0U || options.burstGroupId > 63U
        || options.maximumTerminalPolls < 1
        || options.maximumTerminalPolls > 120) {
        report.failure = QStringLiteral("HIL参数范围无效。");
        return report;
    }

    bool originalKnown = false;
    bool writeAttempted = false;

    const auto restore = [&] {
        if (originalKnown && writeAttempted) {
            UsbRuntimeConfigOperationResultV9 active = port.readConfig(
                UsbRuntimeConfigQueryActiveV9);
            report.steps.push_back(QStringLiteral("cleanup read active success=%1 burst=%2")
                .arg(active.success)
                .arg(active.receipt.activeConfiguration.txBurstCycles));
            if (active.success
                && active.receipt.activeConfiguration.txBurstCycles
                    != options.expectedOriginalBurst) {
                const UsbRuntimeConfigObjectV9 restoreObject = makeApply(
                    active.receipt, options.expectedOriginalBurst,
                    options.burstGroupId, port.nextTransactionId());
                UsbRuntimeConfigOperationResultV9 restored = submitAndResolve(
                    port, restoreObject, options.maximumTerminalPolls,
                    &report.steps);
                active = port.readConfig(UsbRuntimeConfigQueryActiveV9);
                report.originalRestored = terminalSuccess(
                    restored, UsbRuntimeConfigApplyV9)
                    && exactBurstReadback(active,
                                          options.expectedOriginalBurst);
            } else {
                report.originalRestored = exactBurstReadback(
                    active, options.expectedOriginalBurst);
            }
        }
        const ControlAuthorityResultV2 autonomous = port.switchMode(
            kUsbControlModeAutonomousV2);
        const ControlAuthorityResultV2 autonomousReadback = port.readAuthority();
        report.autonomousRestored = autonomous.success
            && autonomousReadback.success
            && autonomousReadback.state.appliedMode
                == kUsbControlModeAutonomousV2;
        report.steps.push_back(QStringLiteral(
            "cleanup autonomous receipt=%1 readback=%2 mode=%3")
            .arg(autonomous.success).arg(autonomousReadback.success)
            .arg(autonomousReadback.state.appliedMode));
    };

    ControlAuthorityResultV2 authority = port.readAuthority();
    report.steps.push_back(QStringLiteral("read authority success=%1 mode=%2 phase=%3")
        .arg(authority.success).arg(authority.state.appliedMode)
        .arg(authority.state.phase));
    if (!authority.success) {
        report.failure = authority.message;
        restore();
        return report;
    }
    if (authority.state.appliedMode != kUsbControlModeAutonomousV2) {
        report.failure = QStringLiteral(
            "HIL启动时设备必须由AUTONOMOUS持有，拒绝接管未知HOST会话。");
        restore();
        return report;
    }
    authority = port.switchMode(kUsbControlModeHostManagedV2);
    report.steps.push_back(QStringLiteral("host takeover success=%1 safe=%2")
        .arg(authority.success).arg(usbHostManagedStoppedV2(authority.state)));
    if (!authority.success || !usbHostManagedStoppedV2(authority.state)) {
        report.failure = authority.success
            ? QStringLiteral("HOST_MANAGED未达到硬件安全停止态。")
            : authority.message;
        restore();
        return report;
    }

    UsbRuntimeConfigOperationResultV9 active = port.readConfig(
        UsbRuntimeConfigQueryActiveV9);
    report.observedOriginalBurst =
        active.receipt.activeConfiguration.txBurstCycles;
    report.steps.push_back(QStringLiteral("read original success=%1 burst=%2 actual=%3")
        .arg(active.success).arg(report.observedOriginalBurst)
        .arg(active.receipt.actualTxBurstCycles));
    if (!exactBurstReadback(active, options.expectedOriginalBurst)) {
        report.failure = QStringLiteral(
            "活动/硬件burst不是命令行声明的预期原值，未执行写入。");
        restore();
        return report;
    }
    originalKnown = true;

    writeAttempted = true;
    const UsbRuntimeConfigObjectV9 temporary = makeApply(
        active.receipt, options.temporaryBurst, options.burstGroupId,
        port.nextTransactionId());
    const UsbRuntimeConfigOperationResultV9 temporaryResult = submitAndResolve(
        port, temporary, options.maximumTerminalPolls, &report.steps);
    active = port.readConfig(UsbRuntimeConfigQueryActiveV9);
    report.observedTemporaryBurst =
        active.receipt.activeConfiguration.txBurstCycles;
    report.temporaryApplied = terminalSuccess(
        temporaryResult, UsbRuntimeConfigApplyV9)
        && exactBurstReadback(active, options.temporaryBurst);
    if (!report.temporaryApplied) {
        report.failure = temporaryResult.message.isEmpty()
            ? QStringLiteral("临时burst应用或回读失败。")
            : temporaryResult.message;
        restore();
        return report;
    }

    const UsbRuntimeConfigObjectV9 restoreObject = makeApply(
        active.receipt, options.expectedOriginalBurst, options.burstGroupId,
        port.nextTransactionId());
    const UsbRuntimeConfigOperationResultV9 restoreResult = submitAndResolve(
        port, restoreObject, options.maximumTerminalPolls, &report.steps);
    active = port.readConfig(UsbRuntimeConfigQueryActiveV9);
    report.originalRestored = terminalSuccess(
        restoreResult, UsbRuntimeConfigApplyV9)
        && exactBurstReadback(active, options.expectedOriginalBurst);
    if (!report.originalRestored) {
        report.failure = restoreResult.message.isEmpty()
            ? QStringLiteral("原burst恢复或回读失败。")
            : restoreResult.message;
        restore();
        return report;
    }

    const UsbRuntimeConfigObjectV9 saveObject = makeSave(
        active.receipt, port.nextTransactionId());
    const UsbRuntimeConfigOperationResultV9 saveResult = submitAndResolve(
        port, saveObject, options.maximumTerminalPolls, &report.steps);
    const UsbRuntimeConfigOperationResultV9 startupReadback = port.readConfig(
        UsbRuntimeConfigQueryStartupV9);
    report.startupSaved = terminalSuccess(
        saveResult, UsbRuntimeConfigSaveStartupV9)
        && saveResult.receipt.persisted
        && exactBurstReadback(saveResult, options.expectedOriginalBurst)
        && exactBurstReadback(startupReadback,
                              options.expectedOriginalBurst);
    report.steps.push_back(QStringLiteral("startup readback success=%1 burst=%2")
        .arg(startupReadback.success)
        .arg(startupReadback.receipt.activeConfiguration.txBurstCycles));
    if (!report.startupSaved) {
        report.failure = saveResult.message.isEmpty()
            ? QStringLiteral("原值SaveStartup失败。") : saveResult.message;
        restore();
        return report;
    }

    restore();
    report.passed = report.temporaryApplied && report.originalRestored
        && report.startupSaved && report.autonomousRestored;
    if (!report.passed && report.failure.isEmpty())
        report.failure = QStringLiteral("最终恢复自主模式失败。");
    return report;
}

} // namespace ucm
