#include "usb_transfer_engine.h"

#include <limits>

namespace ucm {

UsbTransferEngine::UsbTransferEngine(
    quint8 outEndpoint, quint8 inEndpoint,
    UsbBulkTransferFunction transfer)
    : m_outEndpoint(outEndpoint),
      m_inEndpoint(inEndpoint),
      m_transfer(std::move(transfer))
{
}

UsbTransferResult UsbTransferEngine::failure(
    UsbBackendResult backendResult, qint64 transferred,
    const QString &operation)
{
    UsbTransferResult result;
    result.transferred = transferred;
    switch (backendResult) {
    case UsbBackendResult::Timeout:
        ++m_stats.timeouts;
        result.status = UsbTransferStatus::Timeout;
        result.message = QStringLiteral("%1超时。")
                             .arg(operation);
        break;
    case UsbBackendResult::Disconnected:
        ++m_stats.disconnects;
        result.status = UsbTransferStatus::Disconnected;
        result.message = QStringLiteral("%1时设备已断开。")
                             .arg(operation);
        break;
    case UsbBackendResult::Ok:
        ++m_stats.ioErrors;
        result.status = UsbTransferStatus::IoError;
        result.message = QStringLiteral("%1返回零进度。")
                             .arg(operation);
        break;
    case UsbBackendResult::IoError:
        ++m_stats.ioErrors;
        result.status = UsbTransferStatus::IoError;
        result.message = QStringLiteral("%1发生底层 I/O 错误。")
                             .arg(operation);
        break;
    }
    return result;
}

UsbTransferResult UsbTransferEngine::writeAll(
    const QByteArray &bytes, unsigned timeoutMs)
{
    ++m_stats.writeOperations;
    if (!m_transfer || m_outEndpoint == 0U || bytes.isEmpty()
        || bytes.size() > kMaximumTransferBytes
        || bytes.size() > std::numeric_limits<int>::max()) {
        ++m_stats.contractErrors;
        return {UsbTransferStatus::ContractError, 0, {},
                QStringLiteral("USB 写入参数不满足配置链合同。")};
    }

    qint64 offset = 0;
    int operationCalls = 0;
    while (offset < bytes.size()) {
        if (operationCalls >= kMaximumBackendCallsPerOperation) {
            ++m_stats.contractErrors;
            return {UsbTransferStatus::ContractError, offset, {},
                    QStringLiteral("USB 单次写入的 partial transfer 次数超限。")};
        }
        ++operationCalls;
        const int remaining = static_cast<int>(bytes.size() - offset);
        int transferred = 0;
        ++m_stats.backendCalls;
        const UsbBackendResult backendResult = m_transfer(
            m_outEndpoint,
            reinterpret_cast<unsigned char *>(
                const_cast<char *>(bytes.constData() + offset)),
            remaining, &transferred, timeoutMs);
        if (transferred < 0 || transferred > remaining) {
            ++m_stats.contractErrors;
            return {UsbTransferStatus::ContractError, offset, {},
                    QStringLiteral("USB 底层返回了越界写入计数。")};
        }
        if (transferred > 0) {
            offset += transferred;
            m_stats.bytesWritten += static_cast<quint64>(transferred);
            if (transferred < remaining) {
                ++m_stats.partialWrites;
            }
        }
        if (backendResult != UsbBackendResult::Ok || transferred == 0) {
            return failure(backendResult, offset,
                           QStringLiteral("USB bulk 写入"));
        }
    }
    return {UsbTransferStatus::Success, offset, {}, {}};
}

UsbTransferResult UsbTransferEngine::readSome(
    int maximumBytes, unsigned timeoutMs)
{
    ++m_stats.readOperations;
    if (!m_transfer || (m_inEndpoint & 0x80U) == 0U
        || maximumBytes <= 0
        || maximumBytes > kMaximumTransferBytes) {
        ++m_stats.contractErrors;
        return {UsbTransferStatus::ContractError, 0, {},
                QStringLiteral("USB 读取参数不满足配置链合同。")};
    }

    QByteArray bytes(maximumBytes, Qt::Uninitialized);
    int transferred = 0;
    ++m_stats.backendCalls;
    const UsbBackendResult backendResult = m_transfer(
        m_inEndpoint, reinterpret_cast<unsigned char *>(bytes.data()),
        maximumBytes, &transferred, timeoutMs);
    if (transferred < 0 || transferred > maximumBytes) {
        ++m_stats.contractErrors;
        return {UsbTransferStatus::ContractError, 0, {},
                QStringLiteral("USB 底层返回了越界读取计数。")};
    }
    if (transferred > 0) {
        bytes.truncate(transferred);
        m_stats.bytesRead += static_cast<quint64>(transferred);
    } else {
        bytes.clear();
    }
    if (backendResult != UsbBackendResult::Ok) {
        UsbTransferResult result = failure(
            backendResult, transferred, QStringLiteral("USB bulk 读取"));
        result.data = bytes;
        return result;
    }
    if (transferred == 0) {
        return failure(UsbBackendResult::Ok, 0,
                       QStringLiteral("USB bulk 读取"));
    }
    return {UsbTransferStatus::Success, transferred, bytes, {}};
}

} // namespace ucm
