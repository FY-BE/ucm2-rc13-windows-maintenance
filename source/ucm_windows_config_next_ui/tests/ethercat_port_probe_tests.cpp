#include "ethercat_port_probe.h"

#include <QByteArray>

#include <iostream>

namespace {

bool require(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    const QByteArray sourceMac = QByteArray::fromHex("74563c8e2e6c");
    const quint8 index = 0xa7;
    QByteArray frame = EthercatPortScanner::broadcastAlStatusFrame(sourceMac, index);

    bool ok = true;
    ok &= require(frame.size() == 60, "minimum Ethernet frame length");
    ok &= require(frame.mid(6, 6) == sourceMac, "source MAC encoded");
    ok &= require(frame.mid(12, 2) == QByteArray::fromHex("88a4"),
                  "EtherCAT EtherType encoded");
    ok &= require(static_cast<quint8>(frame.at(16)) == 0x07,
                  "BRD command encoded");
    ok &= require(static_cast<quint8>(frame.at(17)) == index,
                  "datagram index encoded");
    ok &= require(frame.mid(20, 2) == QByteArray::fromHex("3001"),
                  "AL Status address encoded little-endian");

    frame[26] = char(0x01);
    frame[27] = char(0x00);
    frame[28] = char(0x01);
    frame[29] = char(0x00);
    int wkc = 0;
    int alStatus = 0;
    ok &= require(EthercatPortScanner::parseAlStatusResponse(
                      frame, index, &wkc, &alStatus),
                  "valid EtherCAT response parsed");
    ok &= require(wkc == 1, "working counter parsed");
    ok &= require(alStatus == 1, "INIT state parsed");
    ok &= require(!EthercatPortScanner::parseAlStatusResponse(
                      frame, static_cast<quint8>(index + 1), nullptr, nullptr),
                  "mismatched datagram rejected");

    frame[28] = char(0x00);
    ok &= require(!EthercatPortScanner::parseAlStatusResponse(
                      frame, index, nullptr, nullptr),
                  "zero working counter rejected");
    ok &= require(EthercatPortScanner::broadcastAlStatusFrame({}, index).isEmpty(),
                  "invalid source MAC rejected");
    return ok ? 0 : 1;
}
