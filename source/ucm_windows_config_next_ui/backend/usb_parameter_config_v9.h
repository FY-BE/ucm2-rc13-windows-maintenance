#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <array>

namespace ucm {

constexpr quint32 kUsbRuntimeConfigTokenV9 = 0x32474643U; // CFG2
constexpr quint32 kUsbRuntimeConfigQueryTokenV9 = 0x32514355U; // UCQ2
constexpr quint32 kUsbRuntimeConfigReceiptTokenV9 = 0x32524355U; // UCR2
constexpr quint32 kUsbRuntimeHardwareStateTokenV9 = 0x32574855U; // UHW2
constexpr quint16 kUsbRuntimeConfigSchemaV9 = 2U;
constexpr quint32 kUsbRuntimeConfigObjectRevisionV9 = 4U;
constexpr int kUsbRuntimeConfigObjectBytesV9 = 384;
constexpr int kUsbRuntimeConfigQueryBytesV9 = 32;
constexpr int kUsbRuntimeConfigReceiptBytesV9 = 768;
constexpr int kUsbRuntimeConfigFieldResultCountV9 = 48;
constexpr int kUsbRuntimeHardwareStateBytesV9 = 112;
constexpr quint32 kUsbRuntimeVcntlDacMinimumV9 = 2621U;
constexpr quint32 kUsbRuntimeVcntlDacMaximumV9 = 39322U;

constexpr quint32 kUsbRuntimeHardwareStateValidV9 = 1U << 0;
constexpr quint32 kUsbRuntimeHardwareStateLoadedV9 = 1U << 1;
constexpr quint32 kUsbRuntimeAgcStageMaximumV9 = 12U;
constexpr quint32 kUsbRuntimeAgcReasonMaximumV9 = 11U;

enum UsbRuntimeConfigOperationV9 : quint32 {
    UsbRuntimeConfigApplyV9 = 1U,
    UsbRuntimeConfigSaveStartupV9 = 2U,
    UsbRuntimeConfigValidateV9 = 3U
};

enum UsbRuntimeConfigQueryKindV9 : quint32 {
    UsbRuntimeConfigQueryLatestV9 = 1U,
    UsbRuntimeConfigQueryActiveV9 = 2U,
    UsbRuntimeConfigQueryStartupV9 = 3U,
    UsbRuntimeConfigQueryTransactionV9 = 4U
};

enum UsbRuntimeConfigReceiptKindV9 : quint32 {
    UsbRuntimeConfigReceiptActiveV9 = 1U,
    UsbRuntimeConfigReceiptAppliedV9 = 2U,
    UsbRuntimeConfigReceiptRejectedV9 = 3U,
    UsbRuntimeConfigReceiptAcceptedV9 = 4U,
    UsbRuntimeConfigReceiptValidatedV9 = 5U,
    UsbRuntimeConfigReceiptSavedStartupV9 = 6U
};

constexpr quint32 kUsbRuntimeAgcEnabledV9 = 1U << 0;
constexpr quint32 kUsbRuntimeAgcFreezeWhenLoadedV9 = 1U << 1;
constexpr quint32 kUsbRuntimeAgcReceiveBeforeHvV9 = 1U << 2;
constexpr quint32 kUsbRuntimeAgcHvBeforeBurstV9 = 1U << 3;
constexpr quint32 kUsbRuntimeAgcFlagsRequiredV9 =
    kUsbRuntimeAgcEnabledV9 | kUsbRuntimeAgcFreezeWhenLoadedV9
    | kUsbRuntimeAgcReceiveBeforeHvV9 | kUsbRuntimeAgcHvBeforeBurstV9;

struct UsbRuntimeConfigObjectV9 {
    quint32 flags = 0;
    quint32 operation = 0;
    quint64 transactionId = 0;
    quint64 baseGeneration = 0;
    quint64 candidateGeneration = 0;
    quint64 presentGroupMask = 0;
    quint64 changedGroupMask = 0;
    quint32 parameterCatalogCrc32 = 0;
    quint32 deviceProfileCrc32 = 0;
    QByteArray objectSha256;

    quint32 deviceModel = 0;
    quint32 measurementHz = 50;
    quint32 rodLengthMm = 0;
    quint32 measurementPointMm = 0;
    quint32 longitudinalVelocityMps = 5900;
    quint32 nominalHvVolts = 50;
    quint32 txBurstCycles = 1;
    // Zero-based start in the 100000-sample PL frame.  This value is always
    // replaced by the active ARM CFG2 readback before an edit is prepared.
    quint32 captureWindowStart = 9781;
    quint32 pgaGainDb = 24;
    quint32 vcntlDacCode = 0;
    quint32 digitalTgcAttenuationDb = 0;
    quint32 minNccPeakMillionths = 0;
    quint32 minNccPeakRatioMillionths = 0;
    qint32 minSnrMilliDb = 0;
    quint32 templateConfirmFrames = 20;
    quint32 minimumValidForceN = 0;
    quint32 biasThresholdMillionths = 0;
    quint32 biasLowLoadGateN = 20000;
    quint32 alarmConfirmFrames = 1;
    quint32 agcFlags = kUsbRuntimeAgcFlagsRequiredV9;
    quint32 agcPeakLowPermille = 600;
    quint32 agcPeakHighPermille = 780;
    quint32 agcEmergencyPermille = 900;
    quint32 agcMaxDelayJitterPs = 0;
    quint32 agcMinimumValidRateMillionths = 0;
    quint32 agcMaximumClippingRateMillionths = 0;
    quint32 agcConfirmFrames = 1;
    quint32 agcSettleFrames = 1;
    quint32 agcMaximumAdjustments = 1;
    quint32 agcVcntlMinimum = 0;
    quint32 agcVcntlMaximum = 65535;
    quint32 agcVcntlFineStep = 1;
    quint32 agcVcntlMediumStep = 1;
    quint32 agcVcntlCoarseStep = 1;
    quint32 agcHvMinimumVolts = 50;
    quint32 agcHvMaximumVolts = 250;
    quint32 agcHvStepVolts = 5;
    quint32 agcBurstMinimum = 1;
    quint32 agcBurstMaximum = 8;
    std::array<quint32, 4> lnaGainDb {{18, 18, 18, 18}};
    std::array<quint32, 4> digitalGainSteps {{0, 0, 0, 0}};
    quint32 agcPgaAllowedMask = 0;
    quint32 agcLnaAllowedMask = 0;
    QByteArray encoded;
};

struct UsbRuntimeConfigReceiptV9 {
    quint32 kind = 0;
    qint32 result = 0;
    bool persisted = false;
    quint32 operation = 0;
    quint64 transactionId = 0;
    quint64 baseGeneration = 0;
    quint64 requestedGeneration = 0;
    quint64 activeGeneration = 0;
    quint64 changedGroupMask = 0;
    UsbRuntimeConfigObjectV9 activeConfiguration;
    std::array<qint32, kUsbRuntimeConfigFieldResultCountV9> fieldResults {};
    quint32 hardwareFlags = 0;
    quint32 agcStage = 0;
    quint32 agcReason = 0;
    quint32 actualMeasurementHz = 0;
    quint32 actualNominalHvVolts = 0;
    quint32 actualTxBurstCycles = 0;
    quint32 actualPgaGainDb = 0;
    quint32 actualVcntlDacCode = 0;
    quint32 actualDigitalTgcAttenuationDb = 0;
    std::array<quint32, 4> actualLnaGainDb {};
    std::array<quint32, 4> actualDigitalGainSteps {};
    quint32 actualValidChannelMask = 0;
    qint32 hardwareResult = 0;
    quint64 hardwarePublishedMonotonicNs = 0;
    quint64 hardwareActionSequence = 0;
    quint64 hardwareGeneration = 0;
    QByteArray hardwareReceipt;
    QByteArray encoded;
};

struct UsbRuntimeConfigOperationResultV9 {
    bool success = false;
    QString message;
    UsbRuntimeConfigReceiptV9 receipt;
};

bool validateUsbRuntimeConfigObjectV9(
    const UsbRuntimeConfigObjectV9 &object, QString *error = nullptr);
QByteArray encodeUsbRuntimeConfigObjectV9(
    const UsbRuntimeConfigObjectV9 &object, QString *error = nullptr);
bool decodeUsbRuntimeConfigObjectV9(
    const QByteArray &payload, UsbRuntimeConfigObjectV9 *object,
    QString *error = nullptr, quint32 expectedCatalogCrc32 = 0U);
QByteArray encodeUsbRuntimeConfigQueryV9(
    quint32 queryKind, quint64 transactionId = 0U,
    QString *error = nullptr);
bool decodeUsbRuntimeConfigReceiptV9(
    const QByteArray &payload, UsbRuntimeConfigReceiptV9 *receipt,
    QString *error = nullptr, quint32 expectedCatalogCrc32 = 0U);
QString usbRuntimeAgcStageTextV9(quint32 stage);
QString usbRuntimeAgcReasonTextV9(quint32 reason);

// A write is admitted only when ARM supplied a current UHW2 snapshot and
// explicitly reports the machine unloaded. Authority/STOP state is checked
// independently at the transport boundary.
constexpr bool usbRuntimeHardwareSafeForWriteV9(quint32 hardwareFlags)
{
    return (hardwareFlags & kUsbRuntimeHardwareStateValidV9) != 0U
        && (hardwareFlags & kUsbRuntimeHardwareStateLoadedV9) == 0U;
}

} // namespace ucm
