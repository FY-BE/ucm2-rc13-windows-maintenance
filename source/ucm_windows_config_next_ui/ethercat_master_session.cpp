#include "ethercat_master_session.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLibrary>
#include <QThread>

#include <algorithm>

namespace ucm::ethercat {
namespace {

#ifdef Q_OS_WIN
struct PcapTimeval { long seconds; long microseconds; };
struct PcapPacketHeader {
    PcapTimeval timestamp;
    quint32 capturedLength;
    quint32 length;
};
using PcapHandle = void;
using OpenLive = PcapHandle *(__cdecl *)(const char *, int, int, int, char *);
using SendPacket = int (__cdecl *)(PcapHandle *, const unsigned char *, int);
using NextPacket = int (__cdecl *)(PcapHandle *, PcapPacketHeader **,
                                    const unsigned char **);
using ClosePcap = void (__cdecl *)(PcapHandle *);
using PcapError = char *(__cdecl *)(PcapHandle *);
using SetMinToCopy = int (__cdecl *)(PcapHandle *, int);
#endif

quint16 read16(const QByteArray &bytes, int offset)
{
    return static_cast<quint8>(bytes[offset])
        | static_cast<quint16>(static_cast<quint8>(bytes[offset + 1])) << 8U;
}

quint32 read32(const QByteArray &bytes, int offset)
{
    return static_cast<quint8>(bytes[offset])
        | static_cast<quint32>(static_cast<quint8>(bytes[offset + 1])) << 8U
        | static_cast<quint32>(static_cast<quint8>(bytes[offset + 2])) << 16U
        | static_cast<quint32>(static_cast<quint8>(bytes[offset + 3])) << 24U;
}

QByteArray little32(quint32 value)
{
    QByteArray bytes;
    bytes.append(static_cast<char>(value & 0xffU));
    bytes.append(static_cast<char>((value >> 8U) & 0xffU));
    bytes.append(static_cast<char>((value >> 16U) & 0xffU));
    bytes.append(static_cast<char>((value >> 24U) & 0xffU));
    return bytes;
}

QByteArray little16(quint16 value)
{
    QByteArray bytes;
    bytes.append(static_cast<char>(value & 0xffU));
    bytes.append(static_cast<char>(value >> 8U));
    return bytes;
}

QByteArray little64(quint64 value)
{
    QByteArray bytes;
    bytes += little32(static_cast<quint32>(value));
    bytes += little32(static_cast<quint32>(value >> 32U));
    return bytes;
}

quint64 read64(const QByteArray &bytes, int offset)
{
    return read32(bytes, offset)
        | static_cast<quint64>(read32(bytes, offset + 4)) << 32U;
}

void appendLittle16(QByteArray *bytes, quint16 value)
{
    bytes->append(static_cast<char>(value & 0xffU));
    bytes->append(static_cast<char>(value >> 8U));
}

struct CyclicReply {
    bool valid = false;
    QByteArray input;
    int workingCounter = 0;
    QString error;
};

QByteArray makeCyclicFrame(const QByteArray &sourceMac, quint8 outputIndex,
                           quint8 inputIndex, const QByteArray &output,
                           int inputBytes)
{
    if (sourceMac.size() != 6 || output.isEmpty() || inputBytes <= 0)
        return {};
    QByteArray datagrams;
    const auto appendDatagram = [&datagrams](Command command, quint8 index,
                                             const QByteArray &data, bool more) {
        datagrams.append(static_cast<char>(command));
        datagrams.append(static_cast<char>(index));
        appendLittle16(&datagrams, 0);      // logical address low word
        appendLittle16(&datagrams, 0x0100); // logical address high word
        appendLittle16(&datagrams, static_cast<quint16>(
            data.size() | (more ? 0x8000U : 0U)));
        appendLittle16(&datagrams, 0);
        datagrams += data;
        appendLittle16(&datagrams, 0);
    };
    appendDatagram(Command::Lwr, outputIndex, output, true);
    appendDatagram(Command::Lrd, inputIndex, QByteArray(inputBytes, 0), false);

    QByteArray frame(6, static_cast<char>(0xff));
    frame += sourceMac;
    frame += QByteArray::fromHex("88a4");
    appendLittle16(&frame, static_cast<quint16>(0x1000U | datagrams.size()));
    frame += datagrams;
    frame.resize(qMax(frame.size(), 60), 0);
    return frame;
}

CyclicReply parseCyclicReply(const QByteArray &frame, quint8 outputIndex,
                             quint8 inputIndex, int outputBytes,
                             int inputBytes)
{
    CyclicReply reply;
    const int firstOffset = 16;
    const int secondOffset = firstOffset + 12 + outputBytes;
    const int endOffset = secondOffset + 12 + inputBytes;
    if (frame.size() < endOffset
        || frame.mid(12, 2) != QByteArray::fromHex("88a4")) {
        reply.error = QStringLiteral("EtherCAT 双数据报帧长度或类型错误");
        return reply;
    }
    if (static_cast<quint8>(frame[firstOffset]) != static_cast<quint8>(Command::Lwr)
        || static_cast<quint8>(frame[firstOffset + 1]) != outputIndex
        || static_cast<quint8>(frame[secondOffset]) != static_cast<quint8>(Command::Lrd)
        || static_cast<quint8>(frame[secondOffset + 1]) != inputIndex) {
        reply.error = QStringLiteral("EtherCAT 双数据报命令或序号不匹配");
        return reply;
    }
    const int firstLength = read16(frame, firstOffset + 6) & 0x07ff;
    const int secondLength = read16(frame, secondOffset + 6) & 0x07ff;
    if (firstLength != outputBytes || secondLength != inputBytes) {
        reply.error = QStringLiteral("EtherCAT 双数据报长度不匹配");
        return reply;
    }
    const quint16 outputWkc = read16(frame, firstOffset + 10 + outputBytes);
    const quint16 inputWkc = read16(frame, secondOffset + 10 + inputBytes);
    if (outputWkc != 1 || inputWkc != 1) {
        reply.error = QStringLiteral("EtherCAT 双数据报 WKC=%1+%2，期望 1+1")
            .arg(outputWkc).arg(inputWkc);
        return reply;
    }
    reply.input = frame.mid(secondOffset + 10, inputBytes);
    reply.workingCounter = outputWkc + inputWkc;
    reply.valid = true;
    return reply;
}

QByteArray smConfig(quint16 address, quint16 length, quint8 control)
{
    QByteArray bytes;
    bytes += little16(address);
    bytes += little16(length);
    bytes.append(static_cast<char>(control));
    bytes.append(char(0));
    bytes.append(char(1));
    bytes.append(char(0));
    return bytes;
}

QByteArray fmmuConfig(quint16 length, quint16 physical, quint8 type)
{
    QByteArray bytes = QByteArray::fromHex("00000001"); // logical 0x01000000
    bytes += little16(length);
    bytes.append(char(0));
    bytes.append(char(7));
    bytes += little16(physical);
    bytes.append(char(0));
    bytes.append(static_cast<char>(type));
    bytes.append(char(1));
    bytes += QByteArray(3, char(0));
    return bytes;
}

QString stateName(int status)
{
    QString value;
    switch (status & 0x0f) {
    case 1: value = QStringLiteral("INIT"); break;
    case 2: value = QStringLiteral("PREOP"); break;
    case 4: value = QStringLiteral("SAFEOP"); break;
    case 8: value = QStringLiteral("OP"); break;
    default: value = QStringLiteral("0x%1").arg(status & 0x0f, 2, 16, QLatin1Char('0')); break;
    }
    if (status & 0x10) value += QStringLiteral("+ERROR");
    return value;
}

class Port final {
public:
    bool open(const QString &adapterId, const QByteArray &mac, QString *error)
    {
#ifndef Q_OS_WIN
        Q_UNUSED(adapterId)
        Q_UNUSED(mac)
        if (error) *error = QStringLiteral("EtherCAT 主站仅支持 Windows");
        return false;
#else
        if (mac.size() != 6) {
            if (error) *error = QStringLiteral("网卡 MAC 长度错误");
            return false;
        }
        sourceMac_ = mac;
        if (!pcap_.load()) {
            if (error) *error = QStringLiteral("无法加载 WinPcap/Npcap");
            return false;
        }
        openLive_ = reinterpret_cast<OpenLive>(pcap_.resolve("pcap_open_live"));
        sendPacket_ = reinterpret_cast<SendPacket>(pcap_.resolve("pcap_sendpacket"));
        nextPacket_ = reinterpret_cast<NextPacket>(pcap_.resolve("pcap_next_ex"));
        closePcap_ = reinterpret_cast<ClosePcap>(pcap_.resolve("pcap_close"));
        pcapError_ = reinterpret_cast<PcapError>(pcap_.resolve("pcap_geterr"));
        setMinToCopy_ = reinterpret_cast<SetMinToCopy>(pcap_.resolve("pcap_setmintocopy"));
        if (!openLive_ || !sendPacket_ || !nextPacket_ || !closePcap_ || !pcapError_) {
            if (error) *error = QStringLiteral("WinPcap/Npcap API 不完整");
            return false;
        }
        char buffer[256] {};
        handle_ = openLive_(adapterId.toLatin1().constData(), 65536, 1, 10, buffer);
        if (!handle_) {
            if (error) *error = QString::fromLocal8Bit(buffer);
            return false;
        }
        // Return each captured frame immediately.  Buffered wake-ups add
        // avoidable latency and make the 1 ms EtherCAT profile unstable.
        if (setMinToCopy_ && setMinToCopy_(handle_, 0) != 0) {
            if (error) *error = QString::fromLocal8Bit(pcapError_(handle_));
            close();
            return false;
        }
        return true;
#endif
    }

    ~Port() { close(); }

    void close()
    {
#ifdef Q_OS_WIN
        if (handle_ && closePcap_) closePcap_(handle_);
        handle_ = nullptr;
#endif
    }

    DatagramReply exchange(Command command, quint16 adp, quint16 ado,
                           const QByteArray &data, int timeoutMs = 250)
    {
        DatagramReply failure;
#ifndef Q_OS_WIN
        Q_UNUSED(command)
        Q_UNUSED(adp)
        Q_UNUSED(ado)
        Q_UNUSED(data)
        Q_UNUSED(timeoutMs)
        failure.error = QStringLiteral("平台不支持");
        return failure;
#else
        const quint8 index = ++index_;
        const QByteArray frame = makeFrame(sourceMac_, command, index, adp, ado, data);
        if (frame.isEmpty() || sendPacket_(handle_,
                reinterpret_cast<const unsigned char *>(frame.constData()), frame.size()) != 0) {
            failure.error = handle_ ? QString::fromLocal8Bit(pcapError_(handle_))
                                    : QStringLiteral("网口未打开");
            return failure;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < timeoutMs) {
            PcapPacketHeader *header = nullptr;
            const unsigned char *packet = nullptr;
            const int status = nextPacket_(handle_, &header, &packet);
            if (status < 0) {
                failure.error = QString::fromLocal8Bit(pcapError_(handle_));
                return failure;
            }
            if (status == 0 || !header || !packet) continue;
            const QByteArray captured(reinterpret_cast<const char *>(packet),
                                      static_cast<int>(header->capturedLength));
            DatagramReply reply = parseReply(captured, command, index, data.size());
            if (reply.valid) return reply;
        }
        failure.error = QStringLiteral("EtherCAT 响应超时");
        return failure;
#endif
    }

    CyclicReply exchangeCyclic(const QByteArray &output, int inputBytes,
                               int timeoutMs = 150)
    {
        CyclicReply failure;
#ifndef Q_OS_WIN
        Q_UNUSED(output)
        Q_UNUSED(inputBytes)
        Q_UNUSED(timeoutMs)
        failure.error = QStringLiteral("平台不支持");
        return failure;
#else
        const quint8 outputIndex = ++index_;
        const quint8 inputIndex = ++index_;
        const QByteArray frame = makeCyclicFrame(
            sourceMac_, outputIndex, inputIndex, output, inputBytes);
        if (frame.isEmpty() || sendPacket_(handle_,
                reinterpret_cast<const unsigned char *>(frame.constData()), frame.size()) != 0) {
            failure.error = handle_ ? QString::fromLocal8Bit(pcapError_(handle_))
                                    : QStringLiteral("网口未打开");
            return failure;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < timeoutMs) {
            PcapPacketHeader *header = nullptr;
            const unsigned char *packet = nullptr;
            const int status = nextPacket_(handle_, &header, &packet);
            if (status < 0) {
                failure.error = QString::fromLocal8Bit(pcapError_(handle_));
                return failure;
            }
            if (status == 0 || !header || !packet) continue;
            const QByteArray captured(reinterpret_cast<const char *>(packet),
                                      static_cast<int>(header->capturedLength));
            CyclicReply reply = parseCyclicReply(
                captured, outputIndex, inputIndex, output.size(), inputBytes);
            if (reply.valid) return reply;
        }
        failure.error = QStringLiteral("EtherCAT 周期双数据报响应超时");
        return failure;
#endif
    }

private:
    QLibrary pcap_ {QStringLiteral("wpcap")};
    QByteArray sourceMac_;
    quint8 index_ = 0x40;
#ifdef Q_OS_WIN
    PcapHandle *handle_ = nullptr;
    OpenLive openLive_ = nullptr;
    SendPacket sendPacket_ = nullptr;
    NextPacket nextPacket_ = nullptr;
    ClosePcap closePcap_ = nullptr;
    PcapError pcapError_ = nullptr;
    SetMinToCopy setMinToCopy_ = nullptr;
#endif
};

void paceUntil(QElapsedTimer *timer, qint64 targetNs)
{
    for (;;) {
        const qint64 remainingNs = targetNs - timer->nsecsElapsed();
        if (remainingNs <= 0) return;
        if (remainingNs > 2000000) {
            QThread::msleep(static_cast<unsigned long>(
                (remainingNs - 1000000) / 1000000));
        } else if (remainingNs > 200000) {
            QThread::usleep(static_cast<unsigned long>(
                (remainingNs - 100000) / 1000));
        } else {
            QThread::yieldCurrentThread();
        }
    }
}

bool exchangeOk(Port *port, Command command, quint16 adp, quint16 ado,
                const QByteArray &data, QByteArray *replyData, QString *error)
{
    const DatagramReply reply = port->exchange(command, adp, ado, data);
    if (!reply.valid) {
        if (error) *error = reply.error;
        return false;
    }
    if (replyData) *replyData = reply.data;
    return true;
}

bool fpwr(Port *port, quint16 ado, const QByteArray &data, QString *error)
{
    return exchangeOk(port, Command::Fpwr, 1001, ado, data, nullptr, error);
}

bool fprd(Port *port, quint16 ado, int length, QByteArray *data, QString *error)
{
    return exchangeOk(port, Command::Fprd, 1001, ado, QByteArray(length, 0), data, error);
}

bool readState(Port *port, int *status, int *code, QString *error)
{
    QByteArray bytes;
    if (!fprd(port, 0x0130, 6, &bytes, error)) return false;
    *status = read16(bytes, 0);
    *code = read16(bytes, 4);
    return true;
}

bool autoIncrementExchange(Port *port, Command command, quint16 ado,
                           const QByteArray &payload, QByteArray *replyData,
                           QString *error)
{
    const DatagramReply reply = port->exchange(command, 0, ado, payload);
    if (!reply.valid) {
        if (error) *error = reply.error;
        return false;
    }
    if (reply.workingCounter != 1) {
        if (error) *error = QStringLiteral("EtherCAT 0x%1 WKC=%2，期望 1")
            .arg(ado, 4, 16, QLatin1Char('0')).arg(reply.workingCounter);
        return false;
    }
    if (replyData) *replyData = reply.data;
    return true;
}

bool siiRead32(Port *port, quint32 wordAddress, quint32 *value, QString *error)
{
    if (!autoIncrementExchange(port, Command::Apwr, 0x0502,
            QByteArray::fromHex("0001") + little32(wordAddress), nullptr, error))
        return false;
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 500) {
        QByteArray status;
        if (!autoIncrementExchange(port, Command::Aprd, 0x0502,
                QByteArray(2, 0), &status, error)) return false;
        if (!(read16(status, 0) & 0x8000)) {
            QByteArray data;
            if (!autoIncrementExchange(port, Command::Aprd, 0x0508,
                    QByteArray(4, 0), &data, error)) return false;
            *value = read32(data, 0);
            return true;
        }
        QThread::msleep(5);
    }
    if (error) *error = QStringLiteral("SII 0x%1 读取超时")
        .arg(wordAddress, 4, 16, QLatin1Char('0'));
    return false;
}

bool verifyIdentity(Port *port, QJsonObject *identity, QString *error)
{
    bool ownershipTaken = autoIncrementExchange(port, Command::Apwr, 0x0500,
                                                 QByteArray(1, 0), nullptr, error);
    quint32 vendor = 0, product = 0, revision = 0, serial = 0;
    bool readOk = ownershipTaken
        && siiRead32(port, 0x0008, &vendor, error)
        && siiRead32(port, 0x000a, &product, error)
        && siiRead32(port, 0x000c, &revision, error)
        && siiRead32(port, 0x000e, &serial, error);
    QString restoreError;
    const bool restored = ownershipTaken
        && autoIncrementExchange(port, Command::Apwr, 0x0500,
                                 QByteArray(1, 1), nullptr, &restoreError);
    const bool match = readOk && vendor == 0x000004d8U
        && product == 0x00009252U && revision == 0x00000001U;
    const auto hex32 = [](quint32 value) {
        return QStringLiteral("0x%1").arg(
            QString::number(value, 16).rightJustified(8, QLatin1Char('0')).toUpper());
    };
    *identity = QJsonObject {
        {QStringLiteral("vendor_id"), hex32(vendor)},
        {QStringLiteral("product_code"), hex32(product)},
        {QStringLiteral("revision"), hex32(revision)},
        {QStringLiteral("serial"), hex32(serial)},
        {QStringLiteral("expected_vendor"), QStringLiteral("0x000004D8")},
        {QStringLiteral("expected_product"), QStringLiteral("0x00009252")},
        {QStringLiteral("expected_revision"), QStringLiteral("0x00000001")},
        {QStringLiteral("identity_match"), match},
        {QStringLiteral("eeprom_owner_restored_to_pdi"), restored}
    };
    if (!restored) {
        if (error) *error = QStringLiteral("EEPROM 控制权未能返还 PDI：%1")
            .arg(restoreError);
        return false;
    }
    if (!readOk) return false;
    if (!match) {
        if (error) *error = QStringLiteral("设备身份不匹配：Vendor 0x%1，Product 0x%2，Revision 0x%3")
            .arg(vendor, 8, 16, QLatin1Char('0'))
            .arg(product, 8, 16, QLatin1Char('0'))
            .arg(revision, 8, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

bool requestState(Port *port, int state, bool acknowledge, int timeoutMs,
                  int *status, int *code, QJsonArray *history, QString *error,
                  std::atomic_bool *stopRequested = nullptr)
{
    if (!fpwr(port, 0x0120, little16(static_cast<quint16>(
            state | (acknowledge ? 0x10 : 0))), error)) return false;
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeoutMs) {
        if (stopRequested && stopRequested->load()) {
            if (error) *error = QStringLiteral("用户停止状态切换");
            return false;
        }
        if (!readState(port, status, code, error)) return false;
        history->append(QJsonObject {
            {QStringLiteral("state"), stateName(*status)},
            {QStringLiteral("al_status"), *status},
            {QStringLiteral("al_status_code"), *code}
        });
        if (*status & 0x10) {
            // An acknowledge request may be observed once before the slave
            // consumes the AL Control write.  Give that transient state a
            // short grace period, then handle any persistent error normally.
            if (acknowledge && ((*status & 0x0f) != state)
                && elapsed.elapsed() < 200) {
                QThread::msleep(1);
                continue;
            }
            if (error) *error = QStringLiteral("%1，AL Status Code 0x%2（%3）")
                .arg(stateName(*status))
                .arg(*code, 4, 16, QLatin1Char('0'))
                .arg(alStatusCodeText(static_cast<quint16>(*code)));
            return false;
        }
        if ((*status & 0x0f) == state) return true;
        QThread::msleep(10);
    }
    if (error) *error = QStringLiteral("等待 %1 超时").arg(stateName(state));
    return false;
}

bool ensureInitBeforeConfigure(Port *port, int *status, int *code,
                               QJsonArray *history, QString *error,
                               bool *recoveredToInit)
{
    const DatagramReply address = port->exchange(
        Command::Apwr, 0, 0x0010, little16(1001));
    if (!address.valid || address.workingCounter != 1) {
        if (error) *error = address.valid
            ? QStringLiteral("EtherCAT 预检站地址 WKC=%1，期望 1")
                  .arg(address.workingCounter)
            : address.error;
        return false;
    }
    if (!readState(port, status, code, error)) return false;
    if (((*status & 0x0f) == 1) && !(*status & 0x10)) return true;

    QString transitionError;
    (void)fpwr(port, 0x0981, QByteArray(1, 0), &transitionError);
    if (requestState(port, 1, (*status & 0x10) != 0, 3000,
                     status, code, history, &transitionError)) {
        if (recoveredToInit) *recoveredToInit = true;
        return true;
    }
    if (error) {
        *error = QStringLiteral("从站应用未处理 INIT 请求：%1；请复位 STM32 网关")
            .arg(transitionError);
    }
    return false;
}

bool configureDcSync0(Port *port, int periodMs, QString *error)
{
    QByteArray systemTime;
    if (!fpwr(port, 0x0980, QByteArray(1, 0), error)
        || !fpwr(port, 0x0981, QByteArray(1, 0), error)
        || !fprd(port, 0x0910, 8, &systemTime, error)) return false;
    const quint64 cycleNs = static_cast<quint64>(periodMs) * 1000000ULL;
    const quint64 now = read64(systemTime, 0);
    // Keep the first SYNC0 pulse in the future while the master establishes
    // valid outputs and submits the SAFEOP -> OP request.  Starting a 1 kHz
    // interrupt stream before that request can starve a loaded slave loop.
    constexpr quint64 startupLeadNs = 500000000ULL;
    const quint64 startupLeadCycles = std::max(
        5ULL, (startupLeadNs + cycleNs - 1ULL) / cycleNs);
    const quint64 start = ((now / cycleNs) + startupLeadCycles) * cycleNs;
    return fpwr(port, 0x09a0, little32(static_cast<quint32>(cycleNs)), error)
        && fpwr(port, 0x09a4, little32(0), error)
        && fpwr(port, 0x0990, little64(start), error)
        && fpwr(port, 0x0981, QByteArray(1, char(0x03)), error);
}

bool configure(Port *port, int *status, int *code,
               QJsonArray *history, QString *error,
               std::atomic_bool *stopRequested)
{
    DatagramReply address = port->exchange(Command::Apwr, 0, 0x0010, little16(1001));
    if (!address.valid) { *error = address.error; return false; }
    if (!fpwr(port, 0x0800, QByteArray(16, 0), error)
        || !fpwr(port, 0x0810, QByteArray(16, 0), error)
        || !fpwr(port, 0x0600, QByteArray(16, 0), error)
        || !fpwr(port, 0x0610, QByteArray(16, 0), error)
        || !fpwr(port, 0x0620, QByteArray(16, 0), error)
        || !fpwr(port, 0x0800, smConfig(0x1000, 128, 0x26), error)
        || !fpwr(port, 0x0808, smConfig(0x1080, 128, 0x22), error))
        return false;
    if (!requestState(port, 2, false, 3000, status, code, history, error,
                      stopRequested)) {
        if (!(*status & 0x10) && (*status & 0x0f) == 1)
            *error = QStringLiteral("从站应用未处理 PREOP 请求：%1；请复位 STM32 网关")
                .arg(*error);
        return false;
    }
    if (!fpwr(port, 0x0810, smConfig(0x1100, 33, 0x64), error)
        || !fpwr(port, 0x0818, smConfig(0x1400, 35, 0x20), error)
        || !fpwr(port, 0x0600, fmmuConfig(33, 0x1100, 2), error)
        || !fpwr(port, 0x0610, fmmuConfig(35, 0x1400, 1), error)) return false;
    if (!requestState(port, 4, false, 9000, status, code, history, error,
                      stopRequested)) {
        if (!(*status & 0x10) && (*status & 0x0f) == 2)
            *error = QStringLiteral("从站应用未处理 SAFEOP 请求：%1；请复位 STM32 网关")
                .arg(*error);
        return false;
    }
    return true;
}

bool cycle(Port *port, const QByteArray &output, int inputBytes,
           SlaveInput *input, int *wkc, QString *error)
{
    const CyclicReply reply = port->exchangeCyclic(output, inputBytes);
    if (!reply.valid) {
        *error = reply.error;
        return false;
    }
    *wkc = reply.workingCounter;
    if (*wkc != 2) {
        *error = QStringLiteral("过程数据 WKC=%1，单从站 LWR+LRD 期望 2").arg(*wkc);
        return false;
    }
    return decodeSlaveInput(reply.input, input, error);
}

QJsonObject registerSnapshot(Port *port)
{
    QJsonObject snapshot;
    const struct { const char *name; quint16 address; int length; } rows[] = {
        {"al_control", 0x0120, 2}, {"al", 0x0130, 6},
        {"sync_latch_pdi_config", 0x0150, 2},
        {"al_event_mask", 0x0204, 4},
        {"al_event_request", 0x0220, 4}, {"sm0_sm3", 0x0800, 32},
        {"fmmu0_fmmu2", 0x0600, 48}, {"pdi_control_config", 0x0140, 16},
        {"watchdog_divider", 0x0400, 2}, {"watchdog_pdi_time", 0x0410, 2},
        {"watchdog_process_time", 0x0420, 2}, {"watchdog_status", 0x0440, 4},
        {"dc_system_time", 0x0910, 8}, {"dc_sync_activation", 0x0980, 2},
        {"dc_sync_pulse_length", 0x0982, 2},
        {"dc_sync_activation_status", 0x0984, 1},
        {"dc_sync0_status", 0x098e, 1},
        {"dc_sync0_start", 0x0990, 8}, {"dc_sync0_cycle", 0x09a0, 4}
    };
    for (const auto &row : rows) {
        QByteArray bytes;
        QString error;
        snapshot.insert(QLatin1String(row.name),
                        fprd(port, row.address, row.length, &bytes, &error)
                            ? QString::fromLatin1(bytes.toHex()) : error);
    }
    return snapshot;
}

} // namespace

MasterRunResult MasterSession::run(const MasterRunOptions &options)
{
    MasterRunResult result;
    QJsonArray history;
    QJsonObject evidence {
        {QStringLiteral("schema"), QStringLiteral("ucm-ethercat-master-hil/v2")},
        {QStringLiteral("observed_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("adapter"), options.adapterId},
        {QStringLiteral("nonvolatile_write"), false},
        {QStringLiteral("protocol"), QStringLiteral("FQX V1.0 LE")},
        {QStringLiteral("application_byte_order"), QStringLiteral("little-endian")},
        {QStringLiteral("firmware_source_commit"), QStringLiteral("b8109b8e07f47f035a346627ba01bd8f02e0585a")},
        {QStringLiteral("firmware_hex_sha256"), QStringLiteral("86123A2567440A62C57E5128ED77FEC8F942291DA2E4B2FE35E425E803FB57EC")},
        {QStringLiteral("esi_sha256"), QStringLiteral("FF0D7AA4EA4D7DD23956CE1AE1462E744FEDA0C2A3B885CC9D34A4CA08BA7A0C")},
        {QStringLiteral("synchronization"), QStringLiteral("DC-SYNC0")},
        {QStringLiteral("rxpdo_bytes"), 33}
    };
    evidence.insert(QStringLiteral("master_output"), QJsonObject {
        {QStringLiteral("machine_model"), options.output.machineModel},
        {QStringLiteral("current_utc_ms"), options.output.currentUtcMs == 0
             ? QStringLiteral("automatic") : QString::number(options.output.currentUtcMs)},
        {QStringLiteral("clamping_force_setpoint_kn"), static_cast<qint64>(options.output.clampingForceSetpointKn)},
        {QStringLiteral("mold_thickness"), static_cast<qint64>(options.output.moldThickness)},
        {QStringLiteral("machine_state"), options.output.machineState},
        {QStringLiteral("device_enabled_utc_ms"), QString::number(options.output.deviceEnabledUtcMs)},
        {QStringLiteral("accumulated_runtime_ms"), QString::number(options.output.accumulatedRuntimeMs)},
        {QStringLiteral("period_ms"), options.periodMs}
    });
    Port port;
    QString error;
    if (!port.open(options.adapterId, options.sourceMac, &error)) {
        result.status = error;
        evidence.insert(QStringLiteral("error"), error);
        result.evidence = evidence;
        return result;
    }

    QJsonObject identity;
    if (!verifyIdentity(&port, &identity, &error)) {
        evidence.insert(QStringLiteral("identity"), identity);
        evidence.insert(QStringLiteral("error"), error);
        result.status = error;
        result.evidence = evidence;
        return result;
    }
    evidence.insert(QStringLiteral("identity"), identity);

    int status = 0;
    int code = 0;
    const int inputBytes = 35;
    bool startupRecoveredToInit = false;
    bool configured = ensureInitBeforeConfigure(
        &port, &status, &code, &history, &error, &startupRecoveredToInit);
    if (configured) {
        configured = configure(&port, &status, &code,
                               &history, &error, options.stopRequested);
    }
    result.inputBytes = inputBytes;
    result.profile = QStringLiteral("FQX V1.0 LE · 33/35 · DC-SYNC0");

    const auto currentOutput = [&options] {
        MasterOutput output = options.output;
        if (output.currentUtcMs == 0)
            output.currentUtcMs = static_cast<quint64>(
                QDateTime::currentMSecsSinceEpoch());
        return encodeMasterOutput(output);
    };
    if (configured) {
        SlaveInput input;
        int wkc = 0;
        result.minimumWkc = 0x7fffffff;
        // First establish valid PDO frames in SAFEOP.  Start DC-SYNC0 only
        // after SAFEOP is stable, matching the commissioning sequence that
        // is known to work with the LAN9252/SSC firmware.
        for (int index = 0; index < 3 && configured; ++index) {
            configured = cycle(&port, currentOutput(), inputBytes, &input, &wkc, &error);
            if (configured) {
                result.minimumWkc = std::min(result.minimumWkc, wkc);
                result.maximumWkc = std::max(result.maximumWkc, wkc);
                result.lastInput = input;
            }
        }
        if (configured)
            configured = configureDcSync0(&port, options.periodMs, &error);
        const int warmupCycles = std::max(
            8, (120 + options.periodMs - 1) / options.periodMs);
        QElapsedTimer warmup;
        warmup.start();
        for (int index = 0; index < warmupCycles && configured; ++index) {
            configured = cycle(&port, currentOutput(), inputBytes, &input, &wkc, &error);
            if (configured) {
                result.minimumWkc = std::min(result.minimumWkc, wkc);
                result.maximumWkc = std::max(result.maximumWkc, wkc);
                result.lastInput = input;
            }
            paceUntil(&warmup,
                      (index + 1LL) * options.periodMs * 1000000LL);
        }
        if (configured && fpwr(&port, 0x0120, little16(8), &error)) {
            QElapsedTimer transition;
            transition.start();
            qint64 transitionCycle = 0;
            const int statePollCycles = std::max(
                1, (20 + options.periodMs - 1) / options.periodMs);
            while (transition.elapsed() < options.opTimeoutMs) {
                if (options.stopRequested && options.stopRequested->load()) {
                    error = QStringLiteral("用户在 OP 切换前停止");
                    break;
                }
                if (!cycle(&port, currentOutput(), inputBytes, &input, &wkc, &error))
                    break;
                result.minimumWkc = std::min(result.minimumWkc, wkc);
                result.maximumWkc = std::max(result.maximumWkc, wkc);
                result.lastInput = input;
                const bool pollState = (transitionCycle % statePollCycles) == 0;
                if (pollState && !readState(&port, &status, &code, &error)) break;
                if (pollState && options.liveUpdate)
                    options.liveUpdate(status, code, wkc, result.completedCycles,
                                       inputBytes, input);
                if (pollState && (status & 0x10)) {
                    error = QStringLiteral("%1，AL Status Code 0x%2（%3）")
                        .arg(stateName(status))
                        .arg(code, 4, 16, QLatin1Char('0'))
                        .arg(alStatusCodeText(static_cast<quint16>(code)));
                    break;
                }
                if (pollState && (status & 0x0f) == 8) {
                    QElapsedTimer schedule;
                    schedule.start();
                    int cycleIndex = 0;
                    int consecutiveDrops = 0;
                    bool cycleFault = false;
                    while ((options.cycles <= 0 || cycleIndex < options.cycles)
                           && !(options.stopRequested
                                && options.stopRequested->load())) {
                        QElapsedTimer one;
                        one.start();
                        if (cycle(&port, currentOutput(), inputBytes, &input, &wkc, &error)) {
                            consecutiveDrops = 0;
                            const bool pollCyclicState =
                                (cycleIndex % statePollCycles) == 0;
                            if (pollCyclicState
                                && !readState(&port, &status, &code, &error)) {
                                cycleFault = true;
                                break;
                            }
                            if (pollCyclicState
                                && ((status & 0x10) || (status & 0x0f) != 8)) {
                                error = QStringLiteral("周期通信中从站退出 OP：%1，0x%2（%3）")
                                    .arg(stateName(status))
                                    .arg(code, 4, 16, QLatin1Char('0'))
                                    .arg(alStatusCodeText(static_cast<quint16>(code)));
                                cycleFault = true;
                                break;
                            }
                            result.minimumWkc = std::min(result.minimumWkc, wkc);
                            result.maximumWkc = std::max(result.maximumWkc, wkc);
                            ++result.completedCycles;
                            result.lastInput = input;
                            if (pollCyclicState && options.liveUpdate)
                                options.liveUpdate(status, code, wkc,
                                                   result.completedCycles,
                                                   inputBytes, input);
                        } else {
                            ++result.droppedCycles;
                            if (++consecutiveDrops >= 3) {
                                error = QStringLiteral("连续 3 个周期失败：%1").arg(error);
                                cycleFault = true;
                                break;
                            }
                        }
                        paceUntil(&schedule,
                                  (cycleIndex + 1LL) * options.periodMs * 1000000LL);
                        ++cycleIndex;
                    }
                    if (!cycleFault
                        && !readState(&port, &status, &code, &error)) {
                        cycleFault = true;
                    } else if (!cycleFault
                               && ((status & 0x10) || (status & 0x0f) != 8)) {
                        error = QStringLiteral("周期通信结束时从站不在 OP：%1，0x%2（%3）")
                            .arg(stateName(status))
                            .arg(code, 4, 16, QLatin1Char('0'))
                            .arg(alStatusCodeText(static_cast<quint16>(code)));
                        cycleFault = true;
                    }
                    const bool requestedStop = options.stopRequested
                        && options.stopRequested->load();
                    result.success = result.completedCycles > 0
                        && result.minimumWkc == 2 && !cycleFault
                        && (requestedStop || options.cycles <= 0
                            || result.completedCycles == options.cycles);
                    if (requestedStop) result.status = QStringLiteral("用户停止周期通信");
                    break;
                }
                paceUntil(&transition,
                          ++transitionCycle * options.periodMs * 1000000LL);
            }
        }
    }

    result.alStatus = status;
    result.alStatusCode = code;
    if (result.minimumWkc == 0x7fffffff) result.minimumWkc = 0;
    if (!result.success) {
        result.status = error.isEmpty() ? QStringLiteral("未进入 OP") : error;
        evidence.insert(QStringLiteral("register_snapshot"), registerSnapshot(&port));
    } else if (result.status.isEmpty()) {
        result.status = QStringLiteral("OP 周期通信完成");
    }
    QString resetError;
    QString dcStopError;
    const bool dcDeactivated = fpwr(&port, 0x0981, QByteArray(1, 0),
                                    &dcStopError);
    result.returnedToInit = requestState(
        &port, 1, (status & 0x10) != 0, 5000,
        &status, &code, &history, &resetError);
    evidence.insert(QStringLiteral("profile"), result.profile);
    evidence.insert(QStringLiteral("txpdo_bytes"), inputBytes);
    evidence.insert(QStringLiteral("state_history"), history);
    evidence.insert(QStringLiteral("completed_cycles"), result.completedCycles);
    evidence.insert(QStringLiteral("dropped_cycles"), result.droppedCycles);
    evidence.insert(QStringLiteral("wkc_min"), result.minimumWkc);
    evidence.insert(QStringLiteral("wkc_max"), result.maximumWkc);
    evidence.insert(QStringLiteral("al_status"), result.alStatus);
    evidence.insert(QStringLiteral("al_status_code"), result.alStatusCode);
    evidence.insert(QStringLiteral("al_status_code_text"),
                    alStatusCodeText(static_cast<quint16>(result.alStatusCode)));
    QJsonObject lastInput {
        {QStringLiteral("data_timestamp_utc_ms"), QString::number(result.lastInput.dataTimestampUtcMs)},
        {QStringLiteral("tie_bar_1_force_kn"), static_cast<qint64>(result.lastInput.tieBar1ForceKn)},
        {QStringLiteral("tie_bar_2_force_kn"), static_cast<qint64>(result.lastInput.tieBar2ForceKn)},
        {QStringLiteral("tie_bar_3_force_kn"), static_cast<qint64>(result.lastInput.tieBar3ForceKn)},
        {QStringLiteral("tie_bar_4_force_kn"), static_cast<qint64>(result.lastInput.tieBar4ForceKn)},
        {QStringLiteral("total_clamping_force_kn"), static_cast<qint64>(result.lastInput.totalClampingForceKn)},
        {QStringLiteral("four_rod_forces_valid"), result.lastInput.rodForcesValid()},
        {QStringLiteral("load_imbalance_rate_0_01_percent"), result.lastInput.loadImbalanceRate},
        {QStringLiteral("fault_flag"), result.lastInput.faultFlag},
        {QStringLiteral("equipment_error_code"), result.lastInput.equipmentErrorCode},
        {QStringLiteral("configuration_error_code"), result.lastInput.configurationErrorCode}
    };
    lastInput.insert(QStringLiteral("windows_rod_sum_kn"),
        result.lastInput.rodForcesValid()
            ? QJsonValue(QString::number(result.lastInput.windowsRodSumKn()))
            : QJsonValue(QJsonValue::Null));
    evidence.insert(QStringLiteral("last_input"), lastInput);
    evidence.insert(QStringLiteral("success"), result.success);
    evidence.insert(QStringLiteral("returned_to_init"), result.returnedToInit);
    if (!result.returnedToInit)
        evidence.insert(QStringLiteral("init_return_error"), resetError);
    evidence.insert(QStringLiteral("dc_sync0_deactivated"), dcDeactivated);
    evidence.insert(QStringLiteral("startup_recovered_to_init"),
                    startupRecoveredToInit);
    if (!dcDeactivated)
        evidence.insert(QStringLiteral("dc_deactivation_error"), dcStopError);
    evidence.insert(QStringLiteral("status"), result.status);
    result.evidence = evidence;
    return result;
}

} // namespace ucm::ethercat
