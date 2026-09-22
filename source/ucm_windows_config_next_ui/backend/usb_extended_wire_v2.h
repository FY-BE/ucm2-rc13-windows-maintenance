#pragma once

#include "config_transport.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace ucm {

constexpr quint16 kUsbExtendedSchemaV2 = 2U;
constexpr quint32 kUsbExtendedCapabilitiesTokenV2 = 0x32435855U;
constexpr quint32 kUsbParameterCatalogTokenV2 = 0x32504355U;
constexpr quint32 kUsbUpgradeTokenV2 = 0x32475555U;
constexpr quint32 kUsbAuthorityStateTokenV2 = 0x32534155U;
constexpr quint32 kUsbAuthorityRequestTokenV2 = 0x32524155U;
constexpr quint32 kUsbAuthorityReceiptTokenV2 = 0x32414155U;
constexpr quint32 kUsbRuntimeStatusTokenV2 = 0x32535255U;
constexpr int kUsbExtendedCapabilitiesBytesV2 = 256;
constexpr int kUsbParameterCatalogHeaderBytesV2 = 32;
constexpr int kUsbParameterDescriptorBytesV2 = 48;
constexpr int kUsbAuthorityStateBytesV2 = 128;
constexpr int kUsbAuthorityRequestBytesV2 = 64;
constexpr int kUsbAuthorityReceiptBytesV2 = 192;
constexpr int kUsbRuntimeStatusBytesV2 = 256;
constexpr quint32 kUsbParameterCatalogMaximumEntriesV2 = 64U;
constexpr quint32 kUsbParameterCatalogMaximumTotalEntriesV2 = 128U;
constexpr int kUsbConfigObjectBytesV2 = 384;
constexpr int kUsbConfigReceiptBytesV2 = 768;
constexpr quint32 kUsbUpgradeChunkMaximumBytesV2 = 65536U;
constexpr quint32 kUsbUpgradeChunkAlignmentBytesV2 = 512U;

constexpr quint64 kUsbExtendedFeatureCapabilitiesV2 = 1ULL << 0;
constexpr quint64 kUsbExtendedFeatureParameterCatalogV2 = 1ULL << 1;
constexpr quint64 kUsbExtendedFeatureConfigV2 = 1ULL << 2;
constexpr quint64 kUsbExtendedFeatureUpgradeStagingV2 = 1ULL << 3;
constexpr quint64 kUsbExtendedFeatureUpgradeActivationV2 = 1ULL << 4;
constexpr quint64 kUsbExtendedFeatureControlAuthorityV2 = 1ULL << 5;
constexpr quint64 kUsbExtendedFeatureRuntimeStatusV2 = 1ULL << 6;
constexpr quint64 kUsbExtendedFeatureCompositeRuntimeStatusV1 = 1ULL << 7;
constexpr quint32 kUsbProductProtocolRevision = 9U;
constexpr quint64 kUsbExtendedFeatureDeviceModelControlV2 = 1ULL << 8;
constexpr quint64 kUsbExtendedFeatureResultSnapshotV1 = 1ULL << 9;
constexpr quint64 kUsbExtendedFeatureLogCatalogV2 = 1ULL << 10;
constexpr quint64 kUsbExtendedFeatureSystemInputPolicyControlV1 = 1ULL << 11;
constexpr quint64 kUsbExtendedFeatureResultInputStatusV1 = 1ULL << 12;
constexpr quint64 kUsbExtendedFeatureResultDiagnosticsV1 = 1ULL << 13;
constexpr quint64 kUsbExtendedFeatureRuntimeControlV1 = 1ULL << 14;


constexpr quint32 kUsbRuntimeStatusDaemonRunningV2 = 1U << 0;
constexpr quint32 kUsbRuntimeStatusCaptureLoopActiveV2 = 1U << 1;
constexpr quint32 kUsbRuntimeStatusHardwareActiveV2 = 1U << 2;
constexpr quint32 kUsbRuntimeStatusLastCaptureAvailableV2 = 1U << 3;
constexpr quint32 kUsbRuntimeStatusLastCanonicalAvailableV2 = 1U << 4;
constexpr quint32 kUsbRuntimeStatusTemplateActiveV2 = 1U << 5;
constexpr quint32 kUsbRuntimeStatusFormalValidV2 = 1U << 6;
constexpr quint32 kUsbRuntimeStatusFormalSinkDegradedV2 = 1U << 7;
constexpr quint32 kUsbRuntimeStatusDisplaySinkDegradedV2 = 1U << 8;
constexpr quint32 kUsbRuntimeStatusTelemetrySinkDegradedV2 = 1U << 9;
constexpr quint32 kUsbRuntimeStatusWaveformSinkDegradedV2 = 1U << 10;
constexpr quint32 kUsbRuntimeStatusSafeWaitV2 = 1U << 11;
constexpr quint32 kUsbRuntimeStatusReconfiguringV2 = 1U << 12;
constexpr quint32 kUsbRuntimeStatusFlagsAllV2 = 0x00001FFFU;
constexpr quint32 kUsbRuntimeStatusMaximumAgeMsV2 = 500U;

constexpr quint32 kUsbRuntimeStartingV2 = 1U;
constexpr quint32 kUsbRuntimeActiveV2 = 2U;
constexpr quint32 kUsbRuntimeSafeConfigurationWaitV2 = 3U;
constexpr quint32 kUsbRuntimeReconfiguringV2 = 4U;
constexpr quint32 kUsbRuntimeStoppingV2 = 5U;

constexpr quint32 kUsbRuntimeCanonicalNoneV2 = 0U;
constexpr quint32 kUsbRuntimeCanonicalTemplatePendingV2 = 1U;
constexpr quint32 kUsbRuntimeCanonicalR3CompleteV2 = 2U;

constexpr quint32 kUsbRuntimeTemplateNoneV2 = 0U;
constexpr quint32 kUsbRuntimeTemplateBuildingV2 = 1U;
constexpr quint32 kUsbRuntimeTemplateActiveV2 = 2U;
constexpr quint32 kUsbRuntimeTemplateRearmingV2 = 3U;

constexpr quint32 kUsbRuntimeSinkUnknownV2 = 0U;
constexpr quint32 kUsbRuntimeSinkAvailableV2 = 1U;
constexpr quint32 kUsbRuntimeSinkUnavailableV2 = 2U;

constexpr quint32 kUsbRuntimeFaultNoneV2 = 0U;
constexpr quint32 kUsbRuntimeFaultHardwareV2 = 1U;
constexpr quint32 kUsbRuntimeFaultCaptureV2 = 2U;
constexpr quint32 kUsbRuntimeFaultPipelineV2 = 3U;
constexpr quint32 kUsbRuntimeFaultFrontendV2 = 4U;
constexpr quint32 kUsbRuntimeFaultConfigurationV2 = 5U;
constexpr quint32 kUsbRuntimeFaultSinkV2 = 6U;
constexpr quint32 kUsbRuntimeFaultInternalV2 = 7U;

constexpr quint32 kUsbControlModeAutonomousV2 = 1U;
constexpr quint32 kUsbControlModeHostManagedV2 = 2U;
constexpr quint32 kUsbControlPhaseSafeStoppedV2 = 1U;
constexpr quint32 kUsbControlPhaseAutonomousBootstrapV2 = 2U;
constexpr quint32 kUsbControlPhaseAutonomousTrackingV2 = 3U;
constexpr quint32 kUsbControlPhaseAutonomousRearmV2 = 4U;
constexpr quint32 kUsbControlPhaseTransitionStoppingV2 = 5U;
constexpr quint32 kUsbControlPhaseTransitionReconfiguringV2 = 6U;
constexpr quint32 kUsbControlPhaseHostActiveV2 = 7U;
constexpr quint32 kUsbControlPhaseHostSafeWaitV2 = 8U;

constexpr quint32 kUsbControlOwnerNoneV2 = 0U;
constexpr quint32 kUsbControlOwnerBootstrapV2 = 1U;
constexpr quint32 kUsbControlOwnerM509V2 = 2U;
constexpr quint32 kUsbControlOwnerHostV2 = 3U;

constexpr quint32 kUsbAuthorityActionSwitchModeV2 = 1U;
constexpr quint32 kUsbAuthorityActionRenewLeaseV2 = 2U;
constexpr quint32 kUsbAuthorityActionResumeHostV2 = 3U;

constexpr quint32 kUsbAuthorityReceiptAcceptedV2 = 1U;
constexpr quint32 kUsbAuthorityReceiptAppliedV2 = 2U;
constexpr quint32 kUsbAuthorityReceiptRejectedV2 = 3U;

struct UsbControlAuthorityReceiptV2 {
    quint32 kind = 0;
    qint32 result = 0;
    quint64 transactionId = 0;
    quint64 expectedGeneration = 0;
    ControlAuthorityStateV2 state;
};

constexpr quint32 kUsbParameterAccessReadV2 = 1U << 0;
constexpr quint32 kUsbParameterAccessWriteSupportedV2 = 1U << 1;
constexpr quint32 kUsbParameterAccessWriteActiveV2 = 1U << 2;
constexpr quint32 kUsbParameterAccessAssetOnlyV2 = 1U << 3;
constexpr quint32 kUsbParameterAccessSessionRestartV2 = 1U << 4;
constexpr quint32 kUsbParameterAccessResealV2 = 1U << 5;
constexpr quint32 kUsbParameterAccessFrozenV2 = 1U << 6;

constexpr quint32 kUsbParameterConstraintRangeV2 = 1U << 0;
constexpr quint32 kUsbParameterConstraintStepV2 = 1U << 1;
constexpr quint32 kUsbParameterConstraintEnumMaskV2 = 1U << 2;
constexpr quint32 kUsbParameterConstraintContextV2 = 1U << 3;
constexpr quint32 kUsbParameterConstraintAllowlistV2 = 1U << 4;

// Stable revision-9 field IDs. IDs 1..83 retain the earlier V2 meanings.
// Config values are distinct from the read-only effective runtime values.
enum UsbParameterFieldIdV2 : quint32 {
    UsbParameterDeviceModelV2 = 4,
    UsbParameterBiasLowLoadGateNV2 = 84,
    UsbParameterMeasurementHzV2 = 96,
    UsbParameterNominalHvVoltsV2 = 97,
    UsbParameterTxBurstCyclesV2 = 98,
    UsbParameterCaptureWindowStartV2 = 99,
    UsbParameterAgcEnabledV2 = 112,
    UsbParameterAgcFreezeWhenLoadedV2 = 113,
    UsbParameterAgcReceiveBeforeHvV2 = 114,
    UsbParameterAgcHvBeforeBurstV2 = 115,
    UsbParameterAgcPeakLowPermilleV2 = 116,
    UsbParameterAgcPeakHighPermilleV2 = 117,
    UsbParameterAgcEmergencyPermilleV2 = 118,
    // 119..121 remain unassigned: fields 64..66 are the single shared
    // NCC / peak-ratio / SNR thresholds used by both measurement and AGC.
    UsbParameterAgcMaxDelayJitterNsV2 = 122,
    UsbParameterAgcMinValidRateV2 = 123,
    UsbParameterAgcMaxClippingRateV2 = 124,
    UsbParameterAgcConfirmFramesV2 = 125,
    UsbParameterAgcSettleFramesV2 = 126,
    UsbParameterAgcMaxAdjustmentsV2 = 127,
    UsbParameterRuntimeLnaDbV2 = 128,
    UsbParameterRuntimePgaDbV2 = 129,
    UsbParameterRuntimeVcntlDacV2 = 130,
    UsbParameterRuntimeDigitalTgcDbV2 = 131,
    UsbParameterRuntimeNominalHvVoltsV2 = 132,
    UsbParameterRuntimeBurstCyclesV2 = 133,
    UsbParameterRuntimeAgcStageV2 = 134,
    UsbParameterRuntimeLoadedV2 = 135,
    UsbParameterRuntimeMeasurementHzV2 = 136,
    UsbParameterAgcVcntlMinimumV2 = 144,
    UsbParameterAgcVcntlMaximumV2 = 145,
    UsbParameterAgcVcntlFineStepV2 = 146,
    UsbParameterAgcVcntlMediumStepV2 = 147,
    UsbParameterAgcVcntlCoarseStepV2 = 148,
    UsbParameterAgcPgaAllowedMaskV2 = 149,
    UsbParameterAgcLnaAllowedMaskV2 = 150,
    UsbParameterAgcHvMinimumV2 = 151,
    UsbParameterAgcHvMaximumV2 = 152,
    UsbParameterAgcHvStepV2 = 153,
    UsbParameterAgcBurstMinimumV2 = 154,
    UsbParameterAgcBurstMaximumV2 = 155
};

enum class UsbParameterWriteChannelV2 {
    ReadOnlyRuntime,
    DeviceModelDocument,
    RuntimeConfiguration
};

QByteArray encodeUsbParameterCatalogRequestV2(
    quint32 startIndex, quint32 maximumEntries,
    quint32 expectedCatalogCrc32, QString *error);

bool decodeUsbExtendedCapabilitiesV2(
    const QByteArray &payload, UsbExtendedCapabilitiesV2 *capabilities,
    QString *error);

bool decodeUsbParameterCatalogChunkV2(
    const QByteArray &payload, quint32 expectedStartIndex,
    quint32 expectedCatalogCrc32, quint32 *totalEntries,
    bool *more, QVector<UsbParameterDescriptorV2> *entries,
    QString *error);

QByteArray encodeUsbControlAuthorityRequestV2(
    quint32 action, quint32 requestedMode, quint64 transactionId,
    quint64 expectedGeneration, QString *error);

bool decodeUsbControlAuthorityStateV2(
    const QByteArray &payload, ControlAuthorityStateV2 *state,
    QString *error);

bool usbHostManagedStoppedV2(const ControlAuthorityStateV2 &state);

bool decodeUsbControlAuthorityReceiptV2(
    const QByteArray &payload, UsbControlAuthorityReceiptV2 *receipt,
    QString *error);

bool decodeUsbRuntimeStatusV2(
    const QByteArray &payload, RuntimeStatusV2 *status, QString *error);

QString usbControlModeTextV2(quint32 mode);
QString usbControlPhaseTextV2(quint32 phase);
QString usbRuntimeRunStateTextV2(quint32 state);
QString usbRuntimeCanonicalPhaseTextV2(quint32 phase);
QString usbRuntimeTemplateStateTextV2(quint32 state);
QString usbRuntimeFaultDomainTextV2(quint32 domain);

QString usbParameterFieldNameV2(quint32 fieldId);
QString usbParameterGroupNameV2(quint16 groupId);
QString usbParameterScopeNameV2(quint8 scope);
QString usbParameterValueKindNameV2(quint8 valueKind);
QString usbParameterAccessTextV2(quint32 accessFlags);
QString usbParameterConstraintTextV2(
    const UsbParameterDescriptorV2 &parameter);
UsbParameterWriteChannelV2 usbParameterWriteChannelV2(quint32 fieldId);
QString usbParameterWriteChannelTextV2(quint32 fieldId);
bool usbParameterEffectivelyWritableV2(
    const UsbParameterDescriptorV2 &parameter, quint64 activeConfigGroupMask);
bool validateR2sWritableParameterCatalogV2(
    const QVector<UsbParameterDescriptorV2> &parameters, QString *error);

} // namespace ucm
