#include "ethercat_protocol.h"

namespace ucm::ethercat {
namespace {

// EtherCAT frame, datagram, register, and WKC fields are little-endian.
void append16(QByteArray *bytes, quint16 value)
{
    bytes->append(static_cast<char>(value & 0xffU));
    bytes->append(static_cast<char>((value >> 8U) & 0xffU));
}

void append32(QByteArray *bytes, quint32 value)
{
    append16(bytes, static_cast<quint16>(value & 0xffffU));
    append16(bytes, static_cast<quint16>(value >> 16U));
}

void append64(QByteArray *bytes, quint64 value)
{
    append32(bytes, static_cast<quint32>(value & 0xffffffffULL));
    append32(bytes, static_cast<quint32>(value >> 32U));
}

quint16 read16(const char *bytes)
{
    return static_cast<quint8>(bytes[0])
        | static_cast<quint16>(static_cast<quint8>(bytes[1])) << 8U;
}

quint32 read32(const char *bytes)
{
    return read16(bytes) | static_cast<quint32>(read16(bytes + 2)) << 16U;
}

quint64 read64(const char *bytes)
{
    return read32(bytes) | static_cast<quint64>(read32(bytes + 4)) << 32U;
}

} // namespace

QByteArray makeFrame(const QByteArray &sourceMac, Command command, quint8 index,
                     quint16 adp, quint16 ado, const QByteArray &data)
{
    if (sourceMac.size() != 6 || data.size() > 0x07ff) return {};
    QByteArray datagram;
    datagram.reserve(12 + data.size());
    datagram.append(static_cast<char>(command));
    datagram.append(static_cast<char>(index));
    append16(&datagram, adp);
    append16(&datagram, ado);
    append16(&datagram, static_cast<quint16>(data.size()));
    append16(&datagram, 0);
    datagram += data;
    append16(&datagram, 0);

    QByteArray frame(6, static_cast<char>(0xff));
    frame += sourceMac;
    frame += QByteArray::fromHex("88a4");
    append16(&frame, static_cast<quint16>(0x1000U | datagram.size()));
    frame += datagram;
    frame.resize(qMax(frame.size(), 60), 0);
    return frame;
}

DatagramReply parseReply(const QByteArray &frame, Command command, quint8 index,
                         int dataLength)
{
    DatagramReply reply;
    const int workingCounterOffset = 26 + dataLength;
    if (dataLength < 0 || frame.size() < workingCounterOffset + 2) {
        reply.error = QStringLiteral("EtherCAT 帧长度不足");
        return reply;
    }
    if (frame.mid(12, 2) != QByteArray::fromHex("88a4")) {
        reply.error = QStringLiteral("EtherType 不是 EtherCAT");
        return reply;
    }
    if (static_cast<quint8>(frame[16]) != static_cast<quint8>(command)
        || static_cast<quint8>(frame[17]) != index) {
        reply.error = QStringLiteral("EtherCAT 命令或序号不匹配");
        return reply;
    }
    const int declaredLength = read16(frame.constData() + 22) & 0x07ff;
    if (declaredLength != dataLength) {
        reply.error = QStringLiteral("EtherCAT 数据长度不匹配");
        return reply;
    }
    reply.data = frame.mid(26, dataLength);
    reply.workingCounter = read16(frame.constData() + workingCounterOffset);
    if (reply.workingCounter == 0) {
        reply.error = QStringLiteral("EtherCAT WKC 为 0");
        return reply;
    }
    reply.valid = true;
    return reply;
}

QString alStatusCodeText(quint16 code)
{
    switch (code) {
    case 0x0000: return QStringLiteral("无错误");
    case 0x0011: return QStringLiteral("无效的状态切换请求");
    case 0x0017: return QStringLiteral("无效的 SyncManager 配置");
    case 0x001a: return QStringLiteral("同步错误");
    case 0x001b: return QStringLiteral("SyncManager 看门狗超时");
    case 0x001c: return QStringLiteral("无效的 SyncManager 类型");
    case 0x001d: return QStringLiteral("无效的输出 PDO 配置");
    case 0x001e: return QStringLiteral("无效的输入 PDO 配置");
    case 0x001f: return QStringLiteral("无效的看门狗配置");
    case 0x0028: return QStringLiteral("从站不支持请求的同步模式");
    case 0x002d: return QStringLiteral("从站未收到 SYNC0 信号");
    case 0x002e: return QStringLiteral("EtherCAT 周期小于从站支持的最小周期");
    case 0x0030: return QStringLiteral("无效的 DC 同步配置");
    case 0x0031: return QStringLiteral("无效的 DC 锁存配置");
    case 0x0032: return QStringLiteral("DC PLL 同步错误");
    case 0x0033: return QStringLiteral("DC SYNC 输入输出错误");
    case 0x0034: return QStringLiteral("DC SYNC 超时");
    case 0x0035: return QStringLiteral("无效的 DC 同步周期");
    case 0x0036: return QStringLiteral("DC SYNC0 周期错误");
    case 0x0037: return QStringLiteral("DC SYNC1 周期错误");
    default: return QStringLiteral("未知 AL 错误");
    }
}

QByteArray encodeMasterOutput(const MasterOutput &output)
{
    QByteArray bytes;
    bytes.reserve(33);
    append16(&bytes, output.machineModel);
    append64(&bytes, output.currentUtcMs);
    append32(&bytes, output.clampingForceSetpointKn);
    append16(&bytes, output.moldThickness);
    bytes.append(static_cast<char>(output.machineState));
    append64(&bytes, output.deviceEnabledUtcMs);
    append64(&bytes, output.accumulatedRuntimeMs);
    return bytes;
}

bool decodeSlaveInput(const QByteArray &bytes, SlaveInput *input, QString *error)
{
    if (!input) {
        if (error) *error = QStringLiteral("从站输出目标为空");
        return false;
    }
    if (bytes.size() != 35) {
        if (error) *error = QStringLiteral("FQX V1.0 从站 TxPDO 必须为 35 字节");
        return false;
    }
    input->dataTimestampUtcMs = read64(bytes.constData());
    input->tieBar1ForceKn = read32(bytes.constData() + 8);
    input->tieBar2ForceKn = read32(bytes.constData() + 12);
    input->tieBar3ForceKn = read32(bytes.constData() + 16);
    input->tieBar4ForceKn = read32(bytes.constData() + 20);
    input->totalClampingForceKn = read32(bytes.constData() + 24);
    input->loadImbalanceRate = read16(bytes.constData() + 28);
    input->faultFlag = static_cast<quint8>(bytes[30]);
    input->equipmentErrorCode = read16(bytes.constData() + 31);
    input->configurationErrorCode = read16(bytes.constData() + 33);
    return true;
}

quint64 SlaveInput::windowsRodSumKn() const
{
    return static_cast<quint64>(tieBar1ForceKn) + tieBar2ForceKn
        + tieBar3ForceKn + tieBar4ForceKn;
}

bool SlaveInput::rodForcesValid() const
{
    return tieBar1ForceKn != kInvalidForceKn
        && tieBar2ForceKn != kInvalidForceKn
        && tieBar3ForceKn != kInvalidForceKn
        && tieBar4ForceKn != kInvalidForceKn;
}

} // namespace ucm::ethercat
