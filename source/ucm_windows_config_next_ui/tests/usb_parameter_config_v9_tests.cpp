#include "usb_parameter_config_v9.h"
#include "usb_extended_wire_v2.h"
#include "usb_wire_v1.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

namespace {
void expect(bool value, const char *message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void p16(QByteArray *b, int o, quint16 v) { v=qToLittleEndian(v); memcpy(b->data()+o,&v,2); }
void p32(QByteArray *b, int o, quint32 v) { v=qToLittleEndian(v); memcpy(b->data()+o,&v,4); }
void p64(QByteArray *b, int o, quint64 v) { v=qToLittleEndian(v); memcpy(b->data()+o,&v,8); }
void seal(QByteArray *b) { p32(b,8,0); p32(b,8,ucm::usbCrc32V1(*b)); }
QByteArray fixture(const char *name) {
    QFile f(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()
        + "/fixtures/arm_product_v9_parameter_config/" + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}

ucm::UsbRuntimeConfigObjectV9 object(quint32 operation=ucm::UsbRuntimeConfigApplyV9)
{
    ucm::UsbRuntimeConfigObjectV9 v;
    v.operation=operation; v.transactionId=0x1020304050607080ULL;
    v.baseGeneration=7; v.candidateGeneration=operation==ucm::UsbRuntimeConfigSaveStartupV9?7:8;
    v.presentGroupMask=0x7ff; v.changedGroupMask=operation==ucm::UsbRuntimeConfigSaveStartupV9?0:0x1e9;
    v.parameterCatalogCrc32=ucm::usbCrc32V1(fixture("parameter_catalog_descriptors.bin"));
    v.deviceProfileCrc32=0x87654321;
    v.deviceModel=1; v.rodLengthMm=1000; v.measurementPointMm=280;
    v.nominalHvVolts=50; v.txBurstCycles=1; v.pgaGainDb=24; v.vcntlDacCode=21845;
    v.captureWindowStart=9781;
    v.digitalTgcAttenuationDb=0; v.minNccPeakMillionths=900000;
    v.minNccPeakRatioMillionths=1100000; v.minSnrMilliDb=12000;
    v.minimumValidForceN=1000; v.biasThresholdMillionths=200000;
    v.biasLowLoadGateN=20000; v.alarmConfirmFrames=5;
    v.agcMaxDelayJitterPs=4000; v.agcMinimumValidRateMillionths=950000;
    v.agcMaximumClippingRateMillionths=1000; v.agcConfirmFrames=5;
    v.agcSettleFrames=4; v.agcMaximumAdjustments=48;
    v.agcVcntlMinimum=2621; v.agcVcntlMaximum=39322;
    v.agcVcntlFineStep=375; v.agcVcntlMediumStep=749; v.agcVcntlCoarseStep=1498;
    v.agcPgaAllowedMask=3; v.agcLnaAllowedMask=7;
    return v;
}

ucm::UsbParameterDescriptorV2 descriptor(quint32 id, bool writable)
{
    ucm::UsbParameterDescriptorV2 p;
    p.fieldId=id; p.groupId=8; p.valueKind=1; p.scope=1;
    p.accessFlags=ucm::kUsbParameterAccessReadV2
        | (writable?ucm::kUsbParameterAccessWriteSupportedV2:0U);
    p.constraintFlags=ucm::kUsbParameterConstraintRangeV2;
    p.minimumValue=0; p.maximumValue=1000000; p.stepValue=0;
    return p;
}

QVector<ucm::UsbParameterDescriptorV2> catalog()
{
    const QByteArray descriptors=fixture("parameter_catalog_descriptors.bin");
    const quint32 crc=ucm::usbCrc32V1(descriptors);
    QByteArray first(32,0);p32(&first,0,ucm::kUsbParameterCatalogTokenV2);
    p16(&first,4,2);p16(&first,6,32);p32(&first,8,crc);
    p32(&first,12,65);p32(&first,16,0);p32(&first,20,64);p32(&first,24,1);
    first.append(descriptors.first(64*48));
    quint32 total=0;bool more=true;QVector<ucm::UsbParameterDescriptorV2> out;QString error;
    expect(ucm::decodeUsbParameterCatalogChunkV2(first,0,crc,&total,&more,&out,&error)
           && total==65 && more && out.size()==64,
           "first canonical descriptor page decodes");
    QByteArray second(32,0);p32(&second,0,ucm::kUsbParameterCatalogTokenV2);
    p16(&second,4,2);p16(&second,6,32);p32(&second,8,crc);
    p32(&second,12,65);p32(&second,16,64);p32(&second,20,1);
    second.append(descriptors.mid(64*48));
    QVector<ucm::UsbParameterDescriptorV2> tail;
    expect(ucm::decodeUsbParameterCatalogChunkV2(second,64,crc,&total,&more,&tail,&error)
           && total==65 && !more && tail.size()==1,
           "second canonical descriptor page decodes field 99");
    out += tail;
    return out;
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QString error;
    auto v=object();
    auto gwPending=v; gwPending.deviceModel=3; gwPending.rodLengthMm=0; gwPending.measurementPointMm=0;
    expect(ucm::validateUsbRuntimeConfigObjectV9(gwPending,&error),"GW pending physical dimensions remain readable");
    auto unsupported=gwPending; unsupported.deviceModel=4;
    expect(!ucm::validateUsbRuntimeConfigObjectV9(unsupported,&error),"unknown runtime device enum rejected");
    auto legacyZero=gwPending; legacyZero.deviceModel=1;
    expect(!ucm::validateUsbRuntimeConfigObjectV9(legacyZero,&error),"A001 zero length still rejected");
    const QByteArray wire=ucm::encodeUsbRuntimeConfigObjectV9(v,&error);
    ucm::UsbRuntimeConfigObjectV9 decoded;
    expect(wire.size()==384,"CFG2 revision4 is exactly 384 bytes");
    expect(wire==fixture("config_apply.bin"),"C++ encoder matches immutable Python apply vector");
    const quint32 catalogCrc=ucm::usbCrc32V1(fixture("parameter_catalog_descriptors.bin"));
    expect(catalogCrc!=0U,"canonical 65-entry catalog CRC is nonzero");
    expect(ucm::decodeUsbRuntimeConfigObjectV9(wire,&decoded,&error,catalogCrc)
        && decoded.nominalHvVolts==50 && decoded.txBurstCycles==1
        && decoded.agcVcntlFineStep==375 && decoded.agcFlags==15
        && decoded.biasLowLoadGateN==20000
        && decoded.captureWindowStart==9781,
        "CFG2 revision4 round trip preserves AGC/HV/burst/window fields");
    expect(ucm::usbRuntimeHardwareSafeForWriteV9(
               ucm::kUsbRuntimeHardwareStateValidV9)
        && !ucm::usbRuntimeHardwareSafeForWriteV9(0)
        && !ucm::usbRuntimeHardwareSafeForWriteV9(
               ucm::kUsbRuntimeHardwareStateValidV9
               | ucm::kUsbRuntimeHardwareStateLoadedV9),
        "runtime write requires valid explicitly unloaded UHW2 state");
    QByteArray bad=wire; bad[132]^=1;
    expect(!ucm::decodeUsbRuntimeConfigObjectV9(bad,&decoded,&error),"CFG2 mutation is rejected");
    auto unsafe=v; unsafe.agcFlags&=~ucm::kUsbRuntimeAgcFreezeWhenLoadedV9;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(unsafe,&error).isEmpty(),"loaded AGC freeze cannot be disabled");
    auto badHv=v; badHv.nominalHvVolts=53;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(badHv,&error).isEmpty(),"HV must be 50..250V in 5V steps");
    auto lowVcntl=v; lowVcntl.vcntlDacCode=2620;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(lowVcntl,&error).isEmpty(),
           "VCNTL setpoint below 2621 is rejected");
    auto highVcntl=v; highVcntl.vcntlDacCode=39323;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(highVcntl,&error).isEmpty(),
           "VCNTL setpoint above 39322 is rejected");
    auto lowVcntlRange=v; lowVcntlRange.agcVcntlMinimum=2620;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(lowVcntlRange,&error).isEmpty(),
           "AGC VCNTL minimum below 2621 is rejected");
    auto highVcntlRange=v; highVcntlRange.agcVcntlMaximum=39323;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(highVcntlRange,&error).isEmpty(),
           "AGC VCNTL maximum above 39322 is rejected");
    auto badDigitalTgc=v; badDigitalTgc.digitalTgcAttenuationDb=6;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(badDigitalTgc,&error).isEmpty(),
           "product digital TGC is fixed at 0 dB");
    auto badDigitalGain=v; badDigitalGain.digitalGainSteps[2]=1;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(badDigitalGain,&error).isEmpty(),
           "product per-rod digital gain is fixed at zero");
    auto badWindow=v; badWindow.captureWindowStart=91809;
    expect(ucm::encodeUsbRuntimeConfigObjectV9(badWindow,&error).isEmpty(),
           "PL capture window start is bounded by the retained 8192-sample window");
    const QByteArray save=ucm::encodeUsbRuntimeConfigObjectV9(object(ucm::UsbRuntimeConfigSaveStartupV9),&error);
    expect(save==fixture("config_save_startup.bin"),"SaveStartup has independent immutable vector");
    expect(ucm::encodeUsbRuntimeConfigQueryV9(ucm::UsbRuntimeConfigQueryActiveV9,0,&error).size()==32,
           "active query has fixed 32-byte object");
    expect(ucm::encodeUsbRuntimeConfigQueryV9(ucm::UsbRuntimeConfigQueryTransactionV9,0,&error).isEmpty(),
           "transaction query requires identity");

    QByteArray receipt(ucm::kUsbRuntimeConfigReceiptBytesV9,0);
    p32(&receipt,0,ucm::kUsbRuntimeConfigReceiptTokenV9);p16(&receipt,4,2);p16(&receipt,6,768);
    p32(&receipt,12,ucm::UsbRuntimeConfigReceiptAppliedV9);p32(&receipt,16,0);
    p32(&receipt,20,0);p32(&receipt,24,ucm::UsbRuntimeConfigApplyV9);p32(&receipt,28,48);
    p64(&receipt,32,v.transactionId);p64(&receipt,40,7);p64(&receipt,48,8);p64(&receipt,56,8);p64(&receipt,64,v.changedGroupMask);
    receipt.replace(80,wire.size(),wire);
    p32(&receipt,656,ucm::kUsbRuntimeHardwareStateTokenV9);p16(&receipt,660,2);p16(&receipt,662,112);p32(&receipt,664,1);
    p32(&receipt,668,2);p32(&receipt,672,50);p32(&receipt,676,50);p32(&receipt,680,1);p32(&receipt,684,24);
    p32(&receipt,688,21845);p32(&receipt,692,0);
    for(int n=0;n<4;++n){p32(&receipt,696+n*4,18);p32(&receipt,712+n*4,0);}
    p32(&receipt,728,0xf);p32(&receipt,732,0);p64(&receipt,736,123456789);p64(&receipt,744,42);p64(&receipt,752,8);
    p32(&receipt,760,8);seal(&receipt);
    ucm::UsbRuntimeConfigReceiptV9 r;
    expect(ucm::decodeUsbRuntimeConfigReceiptV9(receipt,&r,&error,catalogCrc)
           && r.kind==ucm::UsbRuntimeConfigReceiptAppliedV9
           && r.actualTxBurstCycles==1 && r.actualNominalHvVolts==50
           && r.actualLnaGainDb[3]==18 && r.hardwareActionSequence==42
           && r.agcReason==8
           && ucm::usbRuntimeAgcStageTextV9(r.agcStage)==QStringLiteral("LNA扫描")
           && ucm::usbRuntimeAgcReasonTextV9(r.agcReason)==QStringLiteral("扫描完成"),
           "UCR2 768-byte terminal receipt decodes");
    QByteArray badHardwareVcntl=receipt;
    p32(&badHardwareVcntl,688,2620); seal(&badHardwareVcntl);
    expect(!ucm::decodeUsbRuntimeConfigReceiptV9(
               badHardwareVcntl,&r,&error,catalogCrc),
           "UHW2 VCNTL readback below the physical range is rejected");
    QByteArray badAgcReason=receipt;
    p32(&badAgcReason,760,12); seal(&badAgcReason);
    expect(!ucm::decodeUsbRuntimeConfigReceiptV9(
               badAgcReason,&r,&error,catalogCrc),
           "UHW2 AGC reason above the lifecycle enum is rejected");
    QByteArray badHardwareReserved=receipt;
    p32(&badHardwareReserved,764,1); seal(&badHardwareReserved);
    expect(!ucm::decodeUsbRuntimeConfigReceiptV9(
               badHardwareReserved,&r,&error,catalogCrc),
           "UHW2 offset 108 remains strict reserved zero");

    auto c=catalog();
    const QVector<quint32> canonicalOrder {
        4,1,2,3,48,49,50,51,52,53,54,55,56,57,
        16,17,18,19,20,64,65,66,80,81,82,83,84,96,97,98,
        112,116,117,118,122,123,124,125,126,127,
        144,145,146,147,148,149,150,151,152,153,154,155,
        113,114,115,128,129,130,131,132,133,134,135,136,99
    };
    expect(c.size()==canonicalOrder.size(),"canonical catalog has exactly 65 entries");
    for (qsizetype index=0;index<c.size();++index)
        expect(c[index].fieldId==canonicalOrder[index],"canonical catalog wire order is frozen");
    expect(c.size()==65 && ucm::validateR2sWritableParameterCatalogV2(c,&error),
           "complete R2S writable/read-only catalog is accepted");
    const auto fixedDigital = [&c](quint32 id) {
        return std::find_if(c.cbegin(), c.cend(),
            [id](const ucm::UsbParameterDescriptorV2 &entry) {
                return entry.fieldId == id;
            });
    };
    const auto digitalGain=fixedDigital(17U), digitalTgc=fixedDigital(20U);
    expect(digitalGain!=c.cend() && digitalTgc!=c.cend()
           && digitalGain->accessFlags==ucm::kUsbParameterAccessReadV2
           && digitalTgc->accessFlags==ucm::kUsbParameterAccessReadV2
           && digitalGain->minimumValue==0.0 && digitalGain->maximumValue==0.0
           && digitalTgc->minimumValue==0.0 && digitalTgc->maximumValue==0.0,
           "catalog freezes product digital gain and TGC at zero read-only");
    auto writableDigital=c;
    for(auto &entry:writableDigital) if(entry.fieldId==17U)
        entry.accessFlags|=ucm::kUsbParameterAccessWriteSupportedV2;
    expect(!ucm::validateR2sWritableParameterCatalogV2(writableDigital,&error),
           "catalog rejects writable product digital gain");
    auto reordered=c; std::swap(reordered[30],reordered[31]);
    expect(!ucm::validateR2sWritableParameterCatalogV2(reordered,&error),
           "canonical catalog wire order cannot drift");
    auto badRatioCatalog=c;
    for(auto &entry:badRatioCatalog) if(entry.fieldId==65U) entry.maximumValue=1000000.0;
    expect(!ucm::validateR2sWritableParameterCatalogV2(badRatioCatalog,&error),
           "NCC peak ratio uses 1.0..10.0 millionths scale");
    auto badVcntlCatalog=c;
    for(auto &entry:badVcntlCatalog) if(entry.fieldId==130U)
        entry.maximumValue=65535.0;
    expect(!ucm::validateR2sWritableParameterCatalogV2(badVcntlCatalog,&error),
           "VCNTL setpoint, AGC range and runtime readback share one physical range");
    expect(ucm::usbParameterWriteChannelV2(4)==ucm::UsbParameterWriteChannelV2::DeviceModelDocument
        && ucm::usbParameterWriteChannelV2(3)==ucm::UsbParameterWriteChannelV2::DeviceModelDocument
        && ucm::usbParameterWriteChannelV2(56)==ucm::UsbParameterWriteChannelV2::DeviceModelDocument
        && ucm::usbParameterWriteChannelV2(57)==ucm::UsbParameterWriteChannelV2::DeviceModelDocument
        && ucm::usbParameterWriteChannelV2(97)==ucm::UsbParameterWriteChannelV2::RuntimeConfiguration
        && ucm::usbParameterWriteChannelV2(132)==ucm::UsbParameterWriteChannelV2::ReadOnlyRuntime,
        "device model, runtime config and effective state use distinct channels");
    auto runtime=std::find_if(c.begin(),c.end(),[](const auto &entry){return entry.fieldId==136U;});
    expect(runtime!=c.end(),"runtime measurement field exists");
    runtime->accessFlags|=ucm::kUsbParameterAccessWriteSupportedV2;
    expect(!ucm::validateR2sWritableParameterCatalogV2(c,&error),"runtime value cannot masquerade as writable config");
    std::cout<<"PASS usb_parameter_config_v9\n";
    return 0;
}
