#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace ucm {

enum class UsbBackendResult {
    Ok = 0,
    Timeout,
    Disconnected,
    IoError
};

enum class UsbTransferStatus {
    Success = 0,
    Timeout,
    Disconnected,
    IoError,
    ContractError
};

struct UsbTransferResult {
    UsbTransferStatus status = UsbTransferStatus::IoError;
    qint64 transferred = 0;
    QByteArray data;
    QString message;

    bool success() const { return status == UsbTransferStatus::Success; }
};

struct UsbTransferStats {
    quint64 writeOperations = 0;
    quint64 readOperations = 0;
    quint64 backendCalls = 0;
    quint64 partialWrites = 0;
    quint64 bytesWritten = 0;
    quint64 bytesRead = 0;
    quint64 timeouts = 0;
    quint64 disconnects = 0;
    quint64 ioErrors = 0;
    quint64 contractErrors = 0;
};

using UsbBulkTransferFunction = std::function<UsbBackendResult(
    quint8 endpoint, unsigned char *data, int length, int *transferred,
    unsigned timeoutMs)>;

class UsbTransferEngine {
public:
    static constexpr int kMaximumTransferBytes = 1024 * 1024;
    static constexpr int kMaximumBackendCallsPerOperation = 1024;

    UsbTransferEngine(quint8 outEndpoint, quint8 inEndpoint,
                      UsbBulkTransferFunction transfer);

    UsbTransferResult writeAll(const QByteArray &bytes,
                               unsigned timeoutMs);
    UsbTransferResult readSome(int maximumBytes, unsigned timeoutMs);

    const UsbTransferStats &stats() const { return m_stats; }

private:
    UsbTransferResult failure(UsbBackendResult backendResult,
                              qint64 transferred,
                              const QString &operation);

    quint8 m_outEndpoint = 0;
    quint8 m_inEndpoint = 0;
    UsbBulkTransferFunction m_transfer;
    UsbTransferStats m_stats;
};

} // namespace ucm
