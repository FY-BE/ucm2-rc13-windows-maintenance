#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ucm {

struct R3EventRecord {
    int lifecycle = 0;
    qint64 eventId = 0;
    qint64 eventSequence = 0;
    qint64 eventTimestampNs = 0;
    qint64 frameCounter = 0;
    int reasonFamily = 0;
    int reasonCode = 0;
    int severity = 0;
    int stage = 0;
    int scope = 0;
    int processState = -1;
    int formalEligible = -1;
    int agcLifecycle = -1;
};

struct R3EventParseResult {
    bool success = false;
    QString message;
    QVector<R3EventRecord> records;
    bool discardedPartialFirstLine = false;
};

R3EventParseResult parseR3EventCsv(const QByteArray &data,
                                   bool beginsAtFileStart);

} // namespace ucm
