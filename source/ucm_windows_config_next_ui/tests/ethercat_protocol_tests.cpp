#include "ethercat_protocol.h"
#include "fqx_machine_models.h"

#include <iostream>
#include <string>

namespace {
bool require(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    using namespace ucm::ethercat;
    bool ok = true;
    ok &= require(MasterOutput {}.machineState == 4U,
                  "master output defaults to machine state 4");
    ok &= require(MasterOutput {}.moldThickness == 6620U,
                  "master output defaults to 662 mm in FQX 0.1 mm units");
    ok &= require(kFqxMachineModels.size() == 29,
                  "FQX machine model table contains all 29 protocol entries");
    ok &= require(isKnownFqxMachineModel(0x0001U)
                      && isKnownFqxMachineModel(0x0025U)
                      && isKnownFqxMachineModel(0x0047U),
                  "LN, GW and HB model codes are accepted");
    ok &= require(!isKnownFqxMachineModel(0x0000U)
                      && !isKnownFqxMachineModel(0x002aU),
                  "codes outside the protocol model table are rejected");
    ok &= require(std::string(kFqxMachineModels[17].name) == "GW1850R"
                      && kFqxMachineModels[17].code == 0x0025U,
                  "GW1850R replaces GW1800R at code 0x0025");
    MasterOutput output;
    output.machineModel = 0x0102U;
    output.currentUtcMs = 0x030405060708090aULL;
    output.clampingForceSetpointKn = 0x0b0c0d0eU;
    output.moldThickness = 0x0f10U;
    output.machineState = 0x11U;
    output.deviceEnabledUtcMs = 0x1213141516171819ULL;
    output.accumulatedRuntimeMs = 0x1a1b1c1d1e1f2021ULL;
    const QByteArray encoded = encodeMasterOutput(output);
    ok &= require(encoded.size() == 33, "FQX RxPDO is exactly 33 bytes");
    ok &= require(encoded == QByteArray::fromHex(
        "02010a090807060504030e0d0c0b100f11191817161514131221201f1e1d1c1b1a"),
        "FQX RxPDO field order and little endian");

    QByteArray inputBytes = QByteArray::fromHex(
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223");
    SlaveInput input;
    QString error;
    ok &= require(decodeSlaveInput(inputBytes, &input, &error),
                  "FQX TxPDO decoded");
    ok &= require(input.dataTimestampUtcMs == 0x0807060504030201ULL,
                  "data timestamp decoded");
    ok &= require(input.tieBar1ForceKn == 0x0c0b0a09U
                      && input.tieBar4ForceKn == 0x18171615U,
                  "four tie-bar forces decoded");
    ok &= require(input.totalClampingForceKn == 0x1c1b1a19U
                      && input.loadImbalanceRate == 0x1e1dU,
                  "independent total and imbalance decoded");
    ok &= require(input.faultFlag == 0x1fU
                      && input.equipmentErrorCode == 0x2120U
                      && input.configurationErrorCode == 0x2322U,
                  "fault and error fields decoded");
    ok &= require(input.windowsRodSumKn()
                      == quint64(0x0c0b0a09U) + 0x100f0e0dU
                         + 0x14131211U + 0x18171615U,
                  "Windows rod sum remains an independent cross-check");
    ok &= require(input.rodForcesValid(),
                  "ordinary four-rod values are valid for the cross-check");
    input.tieBar1ForceKn = kInvalidForceKn;
    input.tieBar2ForceKn = kInvalidForceKn;
    input.tieBar3ForceKn = kInvalidForceKn;
    input.tieBar4ForceKn = kInvalidForceKn;
    ok &= require(!input.rodForcesValid(),
                  "0xffffffff force sentinels invalidate the four-rod cross-check");
    ok &= require(!decodeSlaveInput(inputBytes.left(34), &input, &error),
                  "short FQX TxPDO rejected");
    ok &= require(!decodeSlaveInput(inputBytes + QByteArray(1, 0), &input, &error),
                  "oversized FQX TxPDO rejected");

    QByteArray frame = makeFrame(QByteArray::fromHex("74563c8e2e6c"),
                                 Command::Fprd, 0x31, 1001, 0x0130,
                                 QByteArray(2, 0));
    ok &= require(frame.size() == 60, "minimum frame size");
    ok &= require(frame.mid(18, 6) == QByteArray::fromHex("e90330010200"),
                  "EtherCAT ADP, ADO and length remain little endian");
    frame[26] = 0x08;
    frame[28] = 0x01;
    DatagramReply reply = parseReply(frame, Command::Fprd, 0x31, 2);
    ok &= require(reply.valid && reply.workingCounter == 1
                      && static_cast<quint8>(reply.data[0]) == 8,
                  "datagram reply parsed");
    frame[28] = 0;
    ok &= require(!parseReply(frame, Command::Fprd, 0x31, 2).valid,
                  "zero WKC rejected");
    ok &= require(alStatusCodeText(0x001b).contains(QStringLiteral("看门狗")),
                  "SyncManager watchdog has operator-readable explanation");
    ok &= require(alStatusCodeText(0x001e).contains(QStringLiteral("输入 PDO")),
                  "invalid input configuration has operator-readable explanation");
    ok &= require(alStatusCodeText(0x0030).contains(QStringLiteral("DC 同步"))
                      && alStatusCodeText(0x0036).contains(QStringLiteral("SYNC0")),
                  "DC configuration failures have operator-readable explanations");
    ok &= require(alStatusCodeText(0x002d).contains(QStringLiteral("未收到 SYNC0")),
                  "missing SYNC0 has the exact SSC failure explanation");
    ok &= require(alStatusCodeText(0xabcd).contains(QStringLiteral("未知")),
                  "unknown AL code remains explicit");
    return ok ? 0 : 1;
}
