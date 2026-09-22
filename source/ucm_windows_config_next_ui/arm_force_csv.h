#pragma once
#include "config_transport.h"
#include <QDateTime>
#include <cmath>

namespace ArmForceCsv {
inline QByteArray header() {
    return "row_kind,observed_utc_ms,publisher_generation,published_monotonic_ns,session_id,sequence,frame_counter,capture_request_id,formal_valid,low_load_bias_invalid,bias_valid_min_total_force_N,imbalance_available,imbalance_percent,measurement_mask,force_available_mask,rod_valid_mask,primary_reason,total_force_N,rod1_force_N,rod2_force_N,rod3_force_N,rod4_force_N,rod1_reason,rod2_reason,rod3_reason,rod4_reason,unavailable_reason\n";
}
inline QByteArray join(const QList<QByteArray> &cells) {
    QByteArray result;
    for (auto cell : cells) {
        if (!result.isEmpty()) result += ',';
        if (cell.contains(',') || cell.contains('"') || cell.contains('\n') || cell.contains('\r')) {
            cell.replace("\"", "\"\""); cell = '"' + cell + '"';
        }
        result += cell;
    }
    return result + '\n';
}
inline QByteArray gap(const QString &reason, qint64 observedMs) {
    QList<QByteArray> cells(27);
    cells[0] = "gap"; cells[1] = QByteArray::number(observedMs); cells[26] = reason.toUtf8();
    return join(cells);
}
inline QByteArray measurement(const ucm::TelemetrySnapshot &s, qint64 observedMs) {
    const bool totalValid = s.formalForceValid && std::isfinite(s.formalTotalN);
    const bool imbalanceAvailable = totalValid && !s.lowLoadBiasInvalid
        && std::isfinite(s.imbalanceIndex);
    QList<QByteArray> cells {"measurement", QByteArray::number(observedMs),
        QByteArray::number(s.generation), QByteArray::number(s.publishedMonotonicNs),
        QByteArray::number(s.sessionId), QByteArray::number(s.sequence),
        QByteArray::number(s.frameCounter), QByteArray::number(s.captureRequestId),
        totalValid ? "1" : "0", s.lowLoadBiasInvalid ? "1" : "0",
        s.diagnosticFieldsAvailable
            ? QByteArray::number(s.biasValidMinTotalForceN) : QByteArray(),
        imbalanceAvailable ? "1" : "0",
        imbalanceAvailable
            ? QByteArray::number(s.imbalanceIndex * 100.0, 'g', 17) : QByteArray(),
        QByteArray::number(s.measurementValidMask),
        QByteArray::number(s.forceAvailableMask), QByteArray::number(s.rodValidMask),
        QByteArray::number(s.primaryReasonCode),
        totalValid ? QByteArray::number(s.formalTotalN, 'g', 17) : QByteArray()};
    for (int rod = 0; rod < 4; ++rod)
        cells.push_back((s.forceAvailableMask & (1U << rod)) && std::isfinite(s.rod[rod].forceN)
            ? QByteArray::number(s.rod[rod].forceN, 'g', 17) : QByteArray());
    for (int rod = 0; rod < 4; ++rod)
        cells.push_back(s.diagnosticFieldsAvailable ? QByteArray::number(s.rod[rod].forceReason) : QByteArray());
    cells.push_back(totalValid ? QByteArray() : s.message.toUtf8());
    return join(cells);
}
}
