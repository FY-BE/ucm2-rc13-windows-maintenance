#pragma once
#include "libusb_backend.h"
#include "config_transport.h"
#include <QJsonArray>
#include <QMap>
#include <functional>

namespace ucm::probe {
// An additional wire boundary: a future transport change cannot turn this
// read-only qualification program into a write/control/upgrade client.
class ReadOnlyBackend final : public LibusbBackend {
public:
    explicit ReadOnlyBackend(std::unique_ptr<LibusbBackend> inner);
    UsbOpenResult open() override;
    void close() override;
    bool isOpen() const override;
    QJsonObject evidence() const override;
    UsbTransferResult writeAll(const QByteArray &, unsigned) override;
    UsbTransferResult readExact(int bytes, unsigned timeout) override;
    std::function<void(const QJsonObject &)> trace;
    quint64 deniedRequests = 0;
private:
    std::unique_ptr<LibusbBackend> m_inner;
    quint32 m_payloadRemaining = 0;
};

class Statistics {
public:
    void connection(bool online);
    void sample(const TelemetrySnapshot &, qint64 observedMs, qint64 requestMs);
    QJsonObject report(bool simulation, qint64 elapsedMs) const;
    quint64 samples = 0, paired = 0, valid = 0, unavailable = 0;
    quint64 lowLoadBiasInvalid = 0, imbalanceAvailable = 0;
    quint64 connects = 0, disconnects = 0, reconnectAttempts = 0;
    quint64 logFiles = 0, logFailures = 0, logBytes = 0, configFailures = 0;
private:
    bool m_online = false;
    qint64 m_previousObserved = -1, m_maxGap = 0, m_maxRequest = 0;
    QMap<qint64, quint64> m_latencyHistogram;
    QMap<quint32, quint64> m_reasons;
    QMap<quint32, quint64> m_biasGates;
};
}
