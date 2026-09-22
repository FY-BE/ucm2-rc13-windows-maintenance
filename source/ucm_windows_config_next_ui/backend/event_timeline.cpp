#include "event_timeline.h"

#include <QList>

#include <array>

namespace ucm {
namespace {

constexpr auto kHeader =
    "lifecycle,event_id,event_seq,event_timestamp_ns,frame_counter,"
    "reason_family,reason_code,severity,stage,scope,process_state,"
    "formal_eligible,agc_lifecycle";
constexpr qsizetype kMaximumInputBytes = 65536;
constexpr qsizetype kMaximumRecords = 1000;

bool parseInteger(const QByteArray &field, qint64 *value)
{
    if (field.isEmpty() || field != field.trimmed()) return false;
    bool ok = false;
    const qint64 parsed = field.toLongLong(&ok, 10);
    if (!ok) return false;
    *value = parsed;
    return true;
}

bool inRange(qint64 value, qint64 minimum, qint64 maximum)
{
    return value >= minimum && value <= maximum;
}

} // namespace

R3EventParseResult parseR3EventCsv(const QByteArray &data,
                                   bool beginsAtFileStart)
{
    R3EventParseResult result;
    if (data.size() > kMaximumInputBytes) {
        result.message = QStringLiteral("R3诊断快照超过64 KiB解析上限。");
        return result;
    }
    if (data.isEmpty()) {
        result.success = true;
        result.message = QStringLiteral("R3诊断日志为空。");
        return result;
    }
    if (!data.endsWith('\n')) {
        result.message = QStringLiteral("R3诊断快照末行不完整。");
        return result;
    }

    QList<QByteArray> lines = data.split('\n');
    if (!lines.isEmpty() && lines.constLast().isEmpty()) lines.removeLast();
    for (QByteArray &line : lines) {
        if (line.endsWith('\r')) line.chop(1);
    }

    qsizetype firstRecord = 0;
    if (beginsAtFileStart) {
        if (lines.isEmpty() || lines.constFirst() != kHeader) {
            result.message = QStringLiteral("R3诊断CSV表头与固定契约不一致。");
            return result;
        }
        firstRecord = 1;
    } else {
        if (lines.isEmpty()) {
            result.success = true;
            result.discardedPartialFirstLine = true;
            result.message = QStringLiteral("尾部快照未包含完整事件。");
            return result;
        }
        lines.removeFirst();
        result.discardedPartialFirstLine = true;
    }

    for (qsizetype lineIndex = firstRecord; lineIndex < lines.size();
         ++lineIndex) {
        const QByteArray &line = lines.at(lineIndex);
        if (line.isEmpty()) continue;
        const QList<QByteArray> fields = line.split(',');
        if (fields.size() != 13) {
            result.message = QStringLiteral("R3诊断CSV第%1行不是13列。")
                .arg(lineIndex + 1);
            result.records.clear();
            return result;
        }

        std::array<qint64, 13> values {};
        for (qsizetype field = 0; field < fields.size(); ++field) {
            if (!parseInteger(fields.at(field), &values[field])) {
                result.message = QStringLiteral("R3诊断CSV第%1行第%2列不是十进制整数。")
                    .arg(lineIndex + 1).arg(field + 1);
                result.records.clear();
                return result;
            }
        }
        if (!inRange(values[0], 0, 2)
            || !inRange(values[5], 0, 24)
            || !inRange(values[6], 0, 140)
            || !inRange(values[7], 0, 3)
            || !inRange(values[8], 0, 13)
            || !inRange(values[9], 0, 6)
            || !inRange(values[10], -1, 8)
            || !inRange(values[11], -1, 1)
            || !inRange(values[12], -1, 12)) {
            result.message = QStringLiteral("R3诊断CSV第%1行包含超出冻结枚举的编号。")
                .arg(lineIndex + 1);
            result.records.clear();
            return result;
        }

        R3EventRecord record;
        record.lifecycle = static_cast<int>(values[0]);
        record.eventId = values[1];
        record.eventSequence = values[2];
        record.eventTimestampNs = values[3];
        record.frameCounter = values[4];
        record.reasonFamily = static_cast<int>(values[5]);
        record.reasonCode = static_cast<int>(values[6]);
        record.severity = static_cast<int>(values[7]);
        record.stage = static_cast<int>(values[8]);
        record.scope = static_cast<int>(values[9]);
        record.processState = static_cast<int>(values[10]);
        record.formalEligible = static_cast<int>(values[11]);
        record.agcLifecycle = static_cast<int>(values[12]);
        result.records.push_back(record);
        if (result.records.size() > kMaximumRecords) {
            result.message = QStringLiteral("R3诊断快照超过1000条事件上限。");
            result.records.clear();
            return result;
        }
    }

    result.success = true;
    result.message = QStringLiteral("已解析%1条R3事件。")
        .arg(result.records.size());
    return result;
}

} // namespace ucm
