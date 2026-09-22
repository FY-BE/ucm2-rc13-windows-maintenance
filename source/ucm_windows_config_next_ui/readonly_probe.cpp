#include "readonly_probe.h"
#include "usb_wire_v1.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace ucm::probe {
ReadOnlyBackend::ReadOnlyBackend(std::unique_ptr<LibusbBackend> inner) : m_inner(std::move(inner)) {}
UsbOpenResult ReadOnlyBackend::open() { m_payloadRemaining = 0; return m_inner->open(); }
void ReadOnlyBackend::close() { m_payloadRemaining = 0; m_inner->close(); }
bool ReadOnlyBackend::isOpen() const { return m_inner->isOpen(); }
QJsonObject ReadOnlyBackend::evidence() const {
    auto value = m_inner->evidence();
    value["readonly_wire_guard"] = true;
    value["denied_requests"] = QString::number(deniedRequests);
    return value;
}
UsbTransferResult ReadOnlyBackend::writeAll(const QByteArray &bytes, unsigned timeout) {
    QString error;
    if (m_payloadRemaining) {
        if (bytes.isEmpty() || quint64(bytes.size()) > m_payloadRemaining) error = QStringLiteral("Unexpected request body length");
    } else {
        UsbFrameV1 frame; quint32 payloadBytes = 0;
        // The shared decoder intentionally accepts responses only. Validate
        // request flags/CRC directly rather than weakening that boundary.
        if (bytes.size() != kUsbWireHeaderBytesV1) error = QStringLiteral("Invalid request header size");
        else {
            auto u16 = [&](int at) { return qFromLittleEndian<quint16>(bytes.constData() + at); };
            auto u32 = [&](int at) { return qFromLittleEndian<quint32>(bytes.constData() + at); };
            auto u64 = [&](int at) { return qFromLittleEndian<quint64>(bytes.constData() + at); };
            QByteArray crcImage = bytes; crcImage.replace(36, 4, QByteArray(4, 0));
            if (u32(0) != kUsbWireMagicV1 || u16(4) != kUsbWireAbiV1 || u16(6) != kUsbWireHeaderBytesV1
                || u32(36) != usbCrc32V1(crcImage) || u32(12) > kUsbWirePayloadMaximumV1)
                error = QStringLiteral("Invalid request header contract or CRC");
            frame.messageType = u16(8); frame.flags = u16(10); frame.sequence = u64(16); frame.transactionId = u64(24);
            payloadBytes = u32(12);
        }
        if (error.isEmpty()) {
            const QList<quint16> allowed {
                1, 7, 16, 17, 20, 24, 25, 26, 30, 31, 37, 38, 39, 40, 41, 45, 48
            };
            if (frame.flags != 0 || !frame.sequence || !allowed.contains(frame.messageType))
                error = QStringLiteral("Message blocked by read-only wire allowlist");
            if (trace) trace({{"message", frame.messageType}, {"sequence", QString::number(frame.sequence)},
                {"transaction", QString::number(frame.transactionId)}, {"payload_bytes", qint64(payloadBytes)},
                {"allowed", error.isEmpty()}});
            if (error.isEmpty()) m_payloadRemaining = payloadBytes;
        }
        if (error.isEmpty()) {
            auto result = m_inner->writeAll(bytes, timeout);
            if (!result.success()) close();
            return result;
        }
    }
    if (!error.isEmpty()) {
        ++deniedRequests; close();
        return {UsbTransferStatus::Disconnected, 0, {}, error};
    }
    auto result = m_inner->writeAll(bytes, timeout);
    if (result.success()) m_payloadRemaining -= quint32(bytes.size()); else close();
    return result;
}
UsbTransferResult ReadOnlyBackend::readExact(int bytes, unsigned timeout) { return m_inner->readExact(bytes, timeout); }

void Statistics::connection(bool online) {
    if (online && !m_online) ++connects;
    if (!online && m_online) ++disconnects;
    m_online = online;
}
void Statistics::sample(const TelemetrySnapshot &s, qint64 observedMs, qint64 requestMs) {
    ++samples;
    if (s.success) ++paired;
    if (s.success && s.formalForceValid) ++valid; else ++unavailable;
    if (s.success && s.formalForceValid && s.lowLoadBiasInvalid)
        ++lowLoadBiasInvalid;
    if (s.success && s.formalForceValid && !s.lowLoadBiasInvalid
        && std::isfinite(s.imbalanceIndex))
        ++imbalanceAvailable;
    if (s.success && s.diagnosticFieldsAvailable
        && s.biasValidMinTotalForceN != 0U)
        ++m_biasGates[s.biasValidMinTotalForceN];
    if (s.success || s.primaryReasonCode) ++m_reasons[s.primaryReasonCode];
    if (m_previousObserved >= 0) m_maxGap = std::max(m_maxGap, observedMs - m_previousObserved);
    m_previousObserved = observedMs;
    requestMs = std::max<qint64>(0, requestMs);
    m_maxRequest = std::max(m_maxRequest, requestMs);
    // Fixed 10 ms buckets, capped at 60 s, keep memory bounded in long runs.
    ++m_latencyHistogram[std::min<qint64>(60000, ((requestMs + 9) / 10) * 10)];
}
QJsonObject Statistics::report(bool simulation, qint64 elapsedMs) const {
    quint64 count = 0; qint64 p95 = 0;
    for (auto it = m_latencyHistogram.cbegin(); it != m_latencyHistogram.cend(); ++it) {
        count += it.value(); p95 = it.key();
        if (count >= (samples * 95 + 99) / 100) break;
    }
    QJsonObject reasons;
    for (auto it = m_reasons.cbegin(); it != m_reasons.cend(); ++it) reasons[QString::number(it.key())] = QString::number(it.value());
    QJsonObject biasGates;
    for (auto it = m_biasGates.cbegin(); it != m_biasGates.cend(); ++it)
        biasGates[QString::number(it.key())] = QString::number(it.value());
    return {{"kind", "ucm-w2-read-only-evidence"}, {"simulation", simulation},
        {"acceptance", "observations_only_not_automatic_hardware_acceptance"},
        {"elapsed_ms", elapsedMs}, {"samples", QString::number(samples)}, {"paired_samples", QString::number(paired)},
        {"formal_valid_samples", QString::number(valid)}, {"unavailable_samples", QString::number(unavailable)},
        {"low_load_bias_invalid_samples", QString::number(lowLoadBiasInvalid)},
        {"imbalance_available_samples", QString::number(imbalanceAvailable)},
        {"bias_valid_min_total_force_n_counts", biasGates},
        {"connects", QString::number(connects)}, {"disconnects", QString::number(disconnects)},
        {"reconnect_attempts", QString::number(reconnectAttempts)}, {"request_max_ms", m_maxRequest},
        {"request_p95_bucket_upper_ms", p95}, {"request_bucket_width_ms", 10}, {"request_bucket_cap_ms", 60000},
        {"observation_max_gap_ms", m_maxGap}, {"arm_reason_counts", reasons},
        {"completed_log_files", QString::number(logFiles)}, {"log_failures", QString::number(logFailures)},
        {"completed_log_bytes", QString::number(logBytes)}, {"config_read_failures", QString::number(configFailures)}};
}
}
