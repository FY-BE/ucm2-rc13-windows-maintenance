#include "ethercat_port_probe.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#include <iptypes.h>
#include <ws2tcpip.h>
#endif

namespace {

quint16 little16(const char *data)
{
    return static_cast<quint16>(static_cast<quint8>(data[0]))
        | static_cast<quint16>(static_cast<quint8>(data[1])) << 8U;
}

void appendLittle16(QByteArray *bytes, quint16 value)
{
    bytes->append(static_cast<char>(value & 0xffU));
    bytes->append(static_cast<char>((value >> 8U) & 0xffU));
}

QString alState(int status)
{
    QString state;
    switch (status & 0x0f) {
    case 1: state = QStringLiteral("INIT"); break;
    case 2: state = QStringLiteral("PREOP"); break;
    case 3: state = QStringLiteral("BOOT"); break;
    case 4: state = QStringLiteral("SAFEOP"); break;
    case 8: state = QStringLiteral("OP"); break;
    default: state = QStringLiteral("未知"); break;
    }
    if ((status & 0x10) != 0) state += QStringLiteral(" + ERROR");
    return state;
}

QString macText(const QByteArray &mac)
{
    QStringList parts;
    for (const char byte : mac)
        parts.push_back(QStringLiteral("%1").arg(static_cast<quint8>(byte), 2,
                                                  16, QLatin1Char('0')));
    return parts.join(QLatin1Char(':')).toUpper();
}

#ifdef Q_OS_WIN

struct PcapIf {
    PcapIf *next;
    char *name;
    char *description;
    void *addresses;
    quint32 flags;
};

struct PcapTimeval { long tvSec; long tvUsec; };
struct PcapPacketHeader { PcapTimeval timestamp; quint32 capturedLength; quint32 length; };

using PcapHandle = void;
using FindAllDevices = int (__cdecl *)(PcapIf **, char *);
using FreeAllDevices = void (__cdecl *)(PcapIf *);
using OpenLive = PcapHandle *(__cdecl *)(const char *, int, int, int, char *);
using SendPacket = int (__cdecl *)(PcapHandle *, const unsigned char *, int);
using NextPacket = int (__cdecl *)(PcapHandle *, PcapPacketHeader **,
                                    const unsigned char **);
using ClosePcap = void (__cdecl *)(PcapHandle *);
using PcapError = char *(__cdecl *)(PcapHandle *);

struct WindowsAdapter {
    QString guid;
    QString name;
    QString description;
    QByteArray mac;
    quint64 speedBits = 0;
    bool up = false;
    bool wired = false;
    bool hasGateway = false;
};

QString normalizedGuid(QString value)
{
    value = value.mid(value.indexOf(QLatin1Char('{')));
    value.remove(QLatin1Char('{'));
    value.remove(QLatin1Char('}'));
    return value.toLower();
}

QHash<QString, WindowsAdapter> windowsAdapters(QString *error)
{
    QHash<QString, WindowsAdapter> result;
    ULONG size = 16 * 1024;
    QByteArray storage(static_cast<int>(size), Qt::Uninitialized);
    ULONG code = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS,
                                      nullptr,
                                      reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data()),
                                      &size);
    if (code == ERROR_BUFFER_OVERFLOW) {
        storage.resize(static_cast<int>(size));
        code = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS,
                                    nullptr,
                                    reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data()),
                                    &size);
    }
    if (code != NO_ERROR) {
        if (error) *error = QStringLiteral("Windows 网卡枚举失败：%1").arg(code);
        return result;
    }
    for (auto *item = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
         item != nullptr; item = item->Next) {
        WindowsAdapter adapter;
        adapter.guid = QString::fromLatin1(item->AdapterName);
        adapter.name = item->FriendlyName ? QString::fromWCharArray(item->FriendlyName) : adapter.guid;
        adapter.description = item->Description ? QString::fromWCharArray(item->Description) : QString();
        adapter.mac = QByteArray(reinterpret_cast<const char *>(item->PhysicalAddress),
                                 static_cast<int>(item->PhysicalAddressLength));
        adapter.speedBits = item->TransmitLinkSpeed;
        adapter.up = item->OperStatus == IfOperStatusUp;
        adapter.wired = item->IfType == IF_TYPE_ETHERNET_CSMACD
            && adapter.mac.size() == 6;
        adapter.hasGateway = item->FirstGatewayAddress != nullptr;
        result.insert(normalizedGuid(adapter.guid), adapter);
    }
    return result;
}

#endif

} // namespace

QByteArray EthercatPortScanner::broadcastAlStatusFrame(
    const QByteArray &sourceMac, quint8 index)
{
    if (sourceMac.size() != 6) return {};
    QByteArray datagram;
    datagram.append(char(0x07)); // BRD
    datagram.append(static_cast<char>(index));
    appendLittle16(&datagram, 0);      // ADP
    appendLittle16(&datagram, 0x0130); // AL Status
    appendLittle16(&datagram, 2);      // two data bytes, last datagram
    appendLittle16(&datagram, 0);      // IRQ
    appendLittle16(&datagram, 0);      // data
    appendLittle16(&datagram, 0);      // WKC

    QByteArray frame(6, char(0xff));
    frame += sourceMac;
    frame.append(char(0x88));
    frame.append(char(0xa4));
    appendLittle16(&frame, static_cast<quint16>(0x1000U | datagram.size()));
    frame += datagram;
    frame.resize(60, char(0));
    return frame;
}

bool EthercatPortScanner::parseAlStatusResponse(
    const QByteArray &frame, quint8 index, int *workingCounter, int *alStatus)
{
    if (frame.size() < 30 || static_cast<quint8>(frame[12]) != 0x88
        || static_cast<quint8>(frame[13]) != 0xa4
        || static_cast<quint8>(frame[16]) != 0x07
        || static_cast<quint8>(frame[17]) != index) return false;
    const int wkc = little16(frame.constData() + 28);
    if (wkc <= 0) return false;
    if (workingCounter) *workingCounter = wkc;
    if (alStatus) *alStatus = little16(frame.constData() + 26);
    return true;
}

EthercatPortProbeResult EthercatPortScanner::scan()
{
    EthercatPortProbeResult result;
    result.statusText = QStringLiteral("未执行 EtherCAT 检测");
#ifndef Q_OS_WIN
    result.statusText = QStringLiteral("EtherCAT 网口检测仅支持 Windows");
    return result;
#else
    QString adapterError;
    const auto adapters = windowsAdapters(&adapterError);
    if (adapters.isEmpty()) {
        result.statusText = adapterError.isEmpty()
            ? QStringLiteral("未发现 Windows 网卡") : adapterError;
        return result;
    }

    QLibrary pcap(QStringLiteral("wpcap"));
    if (!pcap.load()) {
        result.statusText = QStringLiteral("未找到 WinPcap/Npcap，无法发送 EtherCAT 发现帧");
        return result;
    }
    auto findAll = reinterpret_cast<FindAllDevices>(pcap.resolve("pcap_findalldevs"));
    auto freeAll = reinterpret_cast<FreeAllDevices>(pcap.resolve("pcap_freealldevs"));
    auto openLive = reinterpret_cast<OpenLive>(pcap.resolve("pcap_open_live"));
    auto sendPacket = reinterpret_cast<SendPacket>(pcap.resolve("pcap_sendpacket"));
    auto nextPacket = reinterpret_cast<NextPacket>(pcap.resolve("pcap_next_ex"));
    auto closePcap = reinterpret_cast<ClosePcap>(pcap.resolve("pcap_close"));
    auto pcapError = reinterpret_cast<PcapError>(pcap.resolve("pcap_geterr"));
    if (!findAll || !freeAll || !openLive || !sendPacket || !nextPacket
        || !closePcap || !pcapError) {
        result.statusText = QStringLiteral("WinPcap/Npcap API 不完整");
        return result;
    }

    char errorBuffer[256] {};
    PcapIf *devices = nullptr;
    if (findAll(&devices, errorBuffer) != 0) {
        result.statusText = QStringLiteral("WinPcap 网卡枚举失败：%1")
            .arg(QString::fromLocal8Bit(errorBuffer));
        return result;
    }

    int maximumWkc = 0;
    int selectedAl = 0;
    WindowsAdapter selected;
    QString selectedPcapName;
    int wiredCount = 0;
    for (PcapIf *device = devices; device != nullptr; device = device->next) {
        const QString pcapName = QString::fromLatin1(device->name ? device->name : "");
        const auto found = adapters.constFind(normalizedGuid(pcapName));
        if (found == adapters.cend() || !found->wired || !found->up) continue;
        ++wiredCount;
        const WindowsAdapter adapter = *found;
        QVariantMap port {
            {QStringLiteral("name"), adapter.name},
            {QStringLiteral("description"), adapter.description},
            {QStringLiteral("pcap_name"), pcapName},
            {QStringLiteral("mac"), macText(adapter.mac)},
            {QStringLiteral("link_speed_mbps"), adapter.speedBits / 1000000.0},
            {QStringLiteral("has_gateway"), adapter.hasGateway},
            {QStringLiteral("ethercat_detected"), false},
            {QStringLiteral("slave_count"), 0},
            {QStringLiteral("al_status"), 0},
            {QStringLiteral("al_state"), QStringLiteral("--")}
        };

        PcapHandle *handle = openLive(device->name, 65536, 1, 80,
                                      errorBuffer);
        if (!handle) {
            port.insert(QStringLiteral("error"),
                        QString::fromLocal8Bit(errorBuffer));
            result.ports.push_back(port);
            continue;
        }
        int portWkc = 0;
        int portAl = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const quint8 index = static_cast<quint8>(0xa0 + attempt);
            const QByteArray frame = broadcastAlStatusFrame(adapter.mac, index);
            if (sendPacket(handle,
                    reinterpret_cast<const unsigned char *>(frame.constData()),
                    frame.size()) != 0) continue;
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 250;
            while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                PcapPacketHeader *header = nullptr;
                const unsigned char *bytes = nullptr;
                const int packetStatus = nextPacket(handle, &header, &bytes);
                if (packetStatus < 0) break;
                if (packetStatus == 0 || !header || !bytes) continue;
                const QByteArray captured(reinterpret_cast<const char *>(bytes),
                                          static_cast<int>(header->capturedLength));
                int wkc = 0;
                int status = 0;
                if (parseAlStatusResponse(captured, index, &wkc, &status)) {
                    portWkc = std::max(portWkc, wkc);
                    portAl = status;
                    break;
                }
            }
        }
        closePcap(handle);
        if (portWkc > 0) {
            port.insert(QStringLiteral("ethercat_detected"), true);
            port.insert(QStringLiteral("slave_count"), portWkc);
            port.insert(QStringLiteral("al_status"), portAl);
            port.insert(QStringLiteral("al_state"), alState(portAl));
            if (portWkc > maximumWkc) {
                maximumWkc = portWkc;
                selectedAl = portAl;
                selected = adapter;
                selectedPcapName = pcapName;
            }
        }
        result.ports.push_back(port);
    }
    freeAll(devices);

    result.success = true;
    result.detected = maximumWkc > 0;
    result.slaveCount = maximumWkc;
    result.alStatus = selectedAl;
    if (result.detected) {
        result.portName = selected.name;
        result.portDescription = selected.description;
        result.macAddress = macText(selected.mac);
        result.adapterId = selectedPcapName;
        result.statusText = QStringLiteral("检测到 %1 个 EtherCAT 从站 · %2 · %3")
            .arg(maximumWkc).arg(selected.name, alState(selectedAl));
    } else {
        result.statusText = wiredCount == 0
            ? QStringLiteral("未发现已连接的有线物理网口")
            : QStringLiteral("已检测 %1 个有线网口，未收到 EtherCAT 从站回应")
                  .arg(wiredCount);
    }
    result.evidence = {
        {QStringLiteral("schema"), QStringLiteral("ucm-ethercat-readonly-discovery/v1")},
        {QStringLiteral("observed_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("read_only"), true},
        {QStringLiteral("command"), QStringLiteral("BRD")},
        {QStringLiteral("register"), QStringLiteral("0x0130 AL Status")},
        {QStringLiteral("working_counter"), maximumWkc},
        {QStringLiteral("slave_count"), maximumWkc},
        {QStringLiteral("al_status"), selectedAl},
        {QStringLiteral("ports"), result.ports}
    };
    return result;
#endif
}

void EthercatPortProbeWorker::scan()
{
    emit completed(EthercatPortScanner::scan());
}

EthercatPortProbe::EthercatPortProbe(bool enabled, QObject *parent)
    : QObject(parent), m_enabled(enabled)
{
    qRegisterMetaType<EthercatPortProbeResult>();
    m_result.statusText = enabled
        ? QStringLiteral("尚未检测模拟主站设备网口")
        : QStringLiteral("离线预览不检测 EtherCAT 网口");
    m_worker = new EthercatPortProbeWorker;
    m_worker->moveToThread(&m_thread);
    connect(this, &EthercatPortProbe::scanRequested,
            m_worker, &EthercatPortProbeWorker::scan, Qt::QueuedConnection);
    connect(m_worker, &EthercatPortProbeWorker::completed, this,
            [this](const EthercatPortProbeResult &result) {
                m_result = result;
                m_busy = false;
                emit changed();
            }, Qt::QueuedConnection);
    connect(&m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("UCM EtherCAT read-only detector"));
    m_thread.start();
}

EthercatPortProbe::~EthercatPortProbe()
{
    m_thread.quit();
    m_thread.wait();
}

QString EthercatPortProbe::alStateText() const
{
    return m_result.detected ? alState(m_result.alStatus) : QStringLiteral("--");
}

void EthercatPortProbe::scan()
{
    if (!m_enabled || m_busy) return;
    m_busy = true;
    m_result.detected = false;
    m_result.statusText = QStringLiteral("正在检测有线网口与 EtherCAT 从站…");
    emit changed();
    emit scanRequested();
}
