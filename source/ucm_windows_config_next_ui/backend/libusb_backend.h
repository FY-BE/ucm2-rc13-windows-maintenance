#pragma once

#include "usb_transfer_engine.h"

#include <QJsonObject>
#include <QString>

#include <memory>

namespace ucm {

struct UsbDeviceIdentity {
    quint16 vendorId = 0x1D6BU;
    quint16 productId = 0x0105U;
    quint8 configuration = 1U;
    quint8 interfaceNumber = 0U;
    quint8 outEndpoint = 0x01U;
    quint8 inEndpoint = 0x81U;
};

struct UsbOpenResult {
    bool success = false;
    QString message;
    QJsonObject evidence;
};

class LibusbBackend {
public:
    LibusbBackend();
    virtual ~LibusbBackend();

    LibusbBackend(const LibusbBackend &) = delete;
    LibusbBackend &operator=(const LibusbBackend &) = delete;

    static UsbDeviceIdentity requiredIdentity();

    virtual UsbOpenResult open();
    virtual void close();
    virtual bool isOpen() const;

    virtual UsbTransferResult writeAll(const QByteArray &bytes,
                               unsigned timeoutMs);
    UsbTransferResult readSome(int maximumBytes, unsigned timeoutMs);
    virtual UsbTransferResult readExact(int bytes, unsigned timeoutMs);

    UsbTransferStats transferStats() const;
    virtual QJsonObject evidence() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ucm
