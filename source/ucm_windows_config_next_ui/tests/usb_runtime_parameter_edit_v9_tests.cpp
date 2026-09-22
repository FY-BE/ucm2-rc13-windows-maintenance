#include "usb_runtime_parameter_edit_v9.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ucm::UsbRuntimeConfigObjectV9 baseline()
{
    ucm::UsbRuntimeConfigObjectV9 value;
    value.measurementHz = 50;
    value.rodLengthMm = 1000;
    value.measurementPointMm = 280;
    value.longitudinalVelocityMps = 5900;
    value.nominalHvVolts = 50;
    value.txBurstCycles = 1;
    value.captureWindowStart = 9781;
    value.pgaGainDb = 24;
    value.vcntlDacCode = 21845;
    value.minNccPeakMillionths = 900000;
    value.minNccPeakRatioMillionths = 1100000;
    value.minSnrMilliDb = 12000;
    value.templateConfirmFrames = 20;
    value.minimumValidForceN = 1000;
    value.biasThresholdMillionths = 200000;
    value.biasLowLoadGateN = 20000;
    value.alarmConfirmFrames = 5;
    value.agcFlags = ucm::kUsbRuntimeAgcFlagsRequiredV9;
    value.agcPeakLowPermille = 600;
    value.agcPeakHighPermille = 780;
    value.agcEmergencyPermille = 900;
    value.agcMaxDelayJitterPs = 4000;
    value.agcMinimumValidRateMillionths = 950000;
    value.agcMaximumClippingRateMillionths = 1000;
    value.agcConfirmFrames = 5;
    value.agcSettleFrames = 4;
    value.agcMaximumAdjustments = 48;
    value.agcVcntlMinimum = 2621;
    value.agcVcntlMaximum = 39322;
    value.agcVcntlFineStep = 375;
    value.agcVcntlMediumStep = 749;
    value.agcVcntlCoarseStep = 1498;
    value.agcHvMinimumVolts = 50;
    value.agcHvMaximumVolts = 250;
    value.agcHvStepVolts = 5;
    value.agcBurstMinimum = 1;
    value.agcBurstMaximum = 8;
    value.agcPgaAllowedMask = 3;
    value.agcLnaAllowedMask = 7;
    return value;
}

ucm::UsbParameterDescriptorV2 descriptor(quint32 id, quint16 group,
                                          quint8 scope, qint64 minimum,
                                          qint64 maximum, qint64 step = 0,
                                          quint64 enumMask = 0)
{
    ucm::UsbParameterDescriptorV2 value;
    value.fieldId = id;
    value.groupId = group;
    value.valueKind = 1;
    value.scope = scope;
    value.accessFlags = ucm::kUsbParameterAccessReadV2
        | ucm::kUsbParameterAccessWriteSupportedV2;
    value.constraintFlags = ucm::kUsbParameterConstraintRangeV2;
    value.minimumValue = static_cast<double>(minimum);
    value.maximumValue = static_cast<double>(maximum);
    value.stepValue = static_cast<double>(step);
    if (step != 0) value.constraintFlags |= ucm::kUsbParameterConstraintStepV2;
    if (enumMask != 0) {
        value.constraintFlags |= ucm::kUsbParameterConstraintEnumMaskV2;
        value.enumMask = enumMask;
    }
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    auto config = baseline();
    QString error;

    auto lna = descriptor(16, 4, 2, 12, 24, 0,
        (1ULL << 12) | (1ULL << 18) | (1ULL << 24));
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, lna, 1ULL << 3, {16, 2, 24}, &error)
           && config.lnaGainDb[2] == 24,
           "per-rod LNA edit is mapped");
    auto pga = descriptor(18, 3, 1, 24, 30, 0,
        (1ULL << 24) | (1ULL << 30));
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, pga, 1ULL << 2, {18, -1, 30}, &error)
           && config.pgaGainDb == 30,
           "PGA edit is mapped");
    auto vcntl = descriptor(19, 3, 1, 2621, 39322, 1);
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, vcntl, 1ULL << 2, {19, -1, 30000}, &error)
           && config.vcntlDacCode == 30000,
           "VCNTL edit is mapped");
    auto hv = descriptor(ucm::UsbParameterNominalHvVoltsV2, 8, 1,
                         50, 250, 5);
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, hv, 1ULL << 7,
               {ucm::UsbParameterNominalHvVoltsV2, -1, 55}, &error)
           && config.nominalHvVolts == 55,
           "HV 5V step edit is mapped");
    auto burst = descriptor(ucm::UsbParameterTxBurstCyclesV2, 8, 1,
                            1, 8, 1);
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, burst, 1ULL << 7,
               {ucm::UsbParameterTxBurstCyclesV2, -1, 8}, &error)
           && config.txBurstCycles == 8,
           "burst edit is mapped");
    auto lowLoad = descriptor(ucm::UsbParameterBiasLowLoadGateNV2, 7, 1,
                              0, 2147483647, 1);
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, lowLoad, 1ULL << 6,
               {ucm::UsbParameterBiasLowLoadGateNV2, -1, 20000}, &error)
           && config.biasLowLoadGateN == 20000,
           "20kN low-load gate edit is mapped");
    auto captureWindow = descriptor(
        ucm::UsbParameterCaptureWindowStartV2, 9, 1, 0, 91808, 1);
    expect(ucm::applyUsbRuntimeParameterEditV9(
               &config, captureWindow, 1ULL << 8,
               {ucm::UsbParameterCaptureWindowStartV2, -1, 12000}, &error)
           && config.captureWindowStart == 12000,
           "PL capture window start edit is mapped through CFG2");

    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, hv, 1ULL << 7,
               {ucm::UsbParameterNominalHvVoltsV2, -1, 53}, &error),
           "HV value outside 5V step is rejected");
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, lna, 1ULL << 3, {16, 4, 18}, &error),
           "invalid rod index is rejected");
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, vcntl, 0, {19, -1, 20000}, &error),
           "inactive catalog group is rejected");
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, vcntl, 1ULL << 2, {19, -1, 2620}, &error),
           "VCNTL below the physical product range is rejected");
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, vcntl, 1ULL << 2, {19, -1, 39323}, &error),
           "VCNTL above the physical product range is rejected");
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, captureWindow, 1ULL << 8,
               {ucm::UsbParameterCaptureWindowStartV2, -1, 91809}, &error),
           "PL capture window start beyond 91808 is rejected");
    auto digitalGain = descriptor(17, 4, 2, 0, 30, 1);
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, digitalGain, 1ULL << 3, {17, 0, 1}, &error),
           "product digital gain is read-only even if a stale catalog says writable");
    auto digitalTgc = descriptor(20, 3, 1, 0, 42, 6);
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, digitalTgc, 1ULL << 2, {20, -1, 6}, &error),
           "product digital TGC is read-only even if a stale catalog says writable");
    auto frozen = descriptor(ucm::UsbParameterAgcFreezeWhenLoadedV2,
                             8, 1, 1, 1);
    frozen.accessFlags = ucm::kUsbParameterAccessReadV2
        | ucm::kUsbParameterAccessFrozenV2;
    expect(!ucm::applyUsbRuntimeParameterEditV9(
               &config, frozen, 1ULL << 7,
               {ucm::UsbParameterAgcFreezeWhenLoadedV2, -1, 1}, &error),
           "frozen AGC safety order cannot be edited");

    const QVector<quint32> globalIds {
        18U, 19U, 20U, 64U, 65U, 66U, 80U, 81U, 82U, 83U, 84U,
        96U, 97U, 98U, 112U, 116U, 117U, 118U, 122U, 123U, 124U,
        125U, 126U, 127U, 144U, 145U, 146U, 147U, 148U, 149U,
        150U, 151U, 152U, 153U, 154U, 155U,
        ucm::UsbParameterCaptureWindowStartV2
    };
    for (quint32 id : globalIds) {
        qint64 value = 0;
        expect(ucm::usbRuntimeParameterValueV9(config, id, -1,
                                                &value, &error),
               "every writable global CFG2 field has a read mapping");
    }
    for (quint32 id : {16U, 17U}) {
        for (int rod = 0; rod < 4; ++rod) {
            qint64 value = 0;
            expect(ucm::usbRuntimeParameterValueV9(config, id, rod,
                                                    &value, &error),
                   "every per-rod CFG2 field has a read mapping");
        }
    }

    ucm::UsbRuntimeConfigReceiptV9 receipt;
    receipt.activeConfiguration = config;
    receipt.hardwareFlags = ucm::kUsbRuntimeHardwareStateValidV9;
    receipt.actualLnaGainDb = config.lnaGainDb;
    receipt.actualDigitalGainSteps = config.digitalGainSteps;
    receipt.actualPgaGainDb = config.pgaGainDb;
    receipt.actualVcntlDacCode = config.vcntlDacCode;
    receipt.actualDigitalTgcAttenuationDb = config.digitalTgcAttenuationDb;
    receipt.actualMeasurementHz = config.measurementHz;
    receipt.actualNominalHvVolts = config.nominalHvVolts;
    receipt.actualTxBurstCycles = config.txBurstCycles;
    const QVector<ucm::UsbRuntimeParameterEditV9> edits {
        {16, 2, 24}, {18, -1, 30}, {19, -1, 30000},
        {ucm::UsbParameterNominalHvVoltsV2, -1, 55},
        {ucm::UsbParameterTxBurstCyclesV2, -1, 8},
        {ucm::UsbParameterCaptureWindowStartV2, -1, 12000},
        {ucm::UsbParameterBiasLowLoadGateNV2, -1, 20000}
    };
    expect(ucm::verifyUsbRuntimeParameterReadbackV9(receipt, edits, &error),
           "active and hardware readback confirm all edited control fields");
    receipt.actualVcntlDacCode = 29999;
    expect(!ucm::verifyUsbRuntimeParameterReadbackV9(receipt, edits, &error),
           "hardware readback mismatch rejects the operation");

    auto same = config;
    expect(ucm::usbRuntimeConfigurationValuesEqualV9(config, same),
           "value comparison ignores transaction metadata");
    same.agcHvMaximumVolts = 245;
    expect(!ucm::usbRuntimeConfigurationValuesEqualV9(config, same),
           "value comparison covers AGC range");
    same = config;
    same.captureWindowStart = 12001;
    expect(!ucm::usbRuntimeConfigurationValuesEqualV9(config, same),
           "value comparison covers PL capture window start");

    std::cout << "PASS usb_runtime_parameter_edit_v9\n";
    return 0;
}
