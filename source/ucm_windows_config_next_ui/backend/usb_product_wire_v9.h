#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

// Revision 9 read-only payloads. All integers are decoded by explicit LE offsets.
// JSON projections encode 64-bit identities as decimal strings, never doubles.
namespace ucm::productv9
{
enum class Domain
{
    DeviceModel,
    InputPolicy
};
struct State
{
    quint64 bootId = 0, authorityGeneration = 0, sessionId = 0, responseRequestId = 0;
    quint64 activeGeneration = 0, startupGeneration = 0, startupCommitSequence = 0;
    bool activeValid = false, startupValid = false, maintenanceWait = false;
    QByteArray systemSha256, activeId, activeDigest, startupId, startupDigest;
    QJsonObject fields;
};
struct Document
{
    quint64 bootId = 0, authorityGeneration = 0, sessionId = 0, responseRequestId = 0;
    quint64 generation = 0, commitSequence = 0;
    quint32 kind = 0;
    bool present = false;
    QByteArray systemSha256, identitySha256, payloadDigest, json;
    QJsonObject fields, configuration;
};
struct PublicationIdentity
{
    quint64 publisherGeneration = 0, publishedMonotonicNs = 0, sessionId = 0;
    quint64 sequence = 0, frameCounter = 0, captureRequestId = 0;
};
struct Publication
{
    PublicationIdentity identity;
    quint32 flags = 0, reason = 0;
    quint32 predictionMask = 0, qualityDegradedMask = 0;
    quint32 commonTrendOutlierMask = 0;
    bool valid = false, forceAvailable = false, formalTotalAvailable = false;
    QJsonObject fields;
};
constexpr quint32 kPublicationValidV3 = 1U << 0;
constexpr quint32 kPublicationForceAvailableV3 = 1U << 1;
constexpr quint32 kPublicationFormalTotalAvailableV3 = 1U << 2;
constexpr quint32 kPublicationPredictionActiveV3 = 1U << 3;
constexpr quint32 kPublicationPredictionMaskV3 = 0x0FU << 8;
constexpr quint32 kPublicationQualityDegradedMaskV3 = 0x0FU << 12;
constexpr quint32 kPublicationCommonTrendOutlierMaskV3 = 0x0FU << 16;
constexpr quint32 kPublicationAllowedFlagsV3 =
    kPublicationValidV3
    | kPublicationForceAvailableV3
    | kPublicationFormalTotalAvailableV3
    | kPublicationPredictionActiveV3
    | kPublicationPredictionMaskV3
    | kPublicationQualityDegradedMaskV3
    | kPublicationCommonTrendOutlierMaskV3;
static_assert(kPublicationAllowedFlagsV3 == 0x000FFF0FU);
struct InputStatus
{
    PublicationIdentity identity;
    quint32 flags = 0;
    QJsonObject fields;
};
constexpr quint32 kResultDiagnosticsTokenV1 = 0x39445255U; // URD9
constexpr int kResultDiagnosticsBytesV1 = 160;
constexpr quint32 kResultDiagnosticsValidV1 = 1U << 0;
constexpr quint32 kResultDiagnosticsLowLoadBiasInvalidV1 = 1U << 1;
constexpr quint32 kResultDiagnosticsPlcStaleV1 = 1U << 2;
constexpr quint32 kResultDiagnosticsPredictionActiveV1 = 1U << 3;
constexpr quint32 kResultDiagnosticsPredictionMaskV1 = 0x0FU << 8;
constexpr quint32 kResultDiagnosticsQualityDegradedMaskV1 = 0x0FU << 12;
constexpr quint32 kResultDiagnosticsCommonTrendOutlierMaskV1 = 0x0FU << 16;
constexpr quint32 kResultDiagnosticsAllowedFlagsV1 =
    kResultDiagnosticsValidV1
    | kResultDiagnosticsLowLoadBiasInvalidV1
    | kResultDiagnosticsPlcStaleV1
    | kResultDiagnosticsPredictionActiveV1
    | kResultDiagnosticsPredictionMaskV1
    | kResultDiagnosticsQualityDegradedMaskV1
    | kResultDiagnosticsCommonTrendOutlierMaskV1;
static_assert(kResultDiagnosticsAllowedFlagsV1 == 0x000FFF0FU);
struct ResultDiagnostics
{
    quint32 flags = 0;
    quint32 predictionMask = 0, qualityDegradedMask = 0;
    quint32 commonTrendOutlierMask = 0;
    quint64 publisherGeneration = 0, publishedMonotonicNs = 0;
    quint64 sessionId = 0, sequence = 0, frameCounter = 0;
    quint32 rodValidMask = 0, biasValidMinTotalForceN = 0;
    double strainMicrostrain[4] {};
    quint32 invalidReason[4] {};
    quint32 plcState = 0, agcState = 0, agcReason = 0;
    quint32 actualPgaDb = 0, actualLnaDb[4] {};
    quint32 actualVcntlDac = 0, actualHvVolts = 0, actualBurstCycles = 0;
};
struct LogEntry
{
    QString name;
    quint32 flags = 0, fileIdentityCrc32 = 0;
    quint64 totalBytes = 0, modifiedTimeNs = 0, snapshotId = 0;
    quint32 kind = 1; // 1: legacy URS1/URS2, 2: native R2S URL3
};
struct LogCatalog
{
    quint64 snapshotId = 0;
    quint32 totalEntries = 0;
    QVector<LogEntry> entries;
};
struct LogChunk
{
    quint64 offset = 0, totalBytes = 0;
    bool more = false;
    QByteArray data;
};
quint32 crc32(const QByteArray &bytes);
// Pure canonical identity calculation for a draft; does not apply or save it.
// Generation > INT64_MAX is deliberately rejected by the Qt JSON boundary.
bool computeDocumentIdentity(Domain, const QByteArray &json, QByteArray *identity, QString *error = nullptr);
// DeviceModel schema 2 remains readable. Schema 3 adds the shared,
// zero-anchored force correction; InputPolicy and the outer USB revision stay unchanged.
// Embedded identity may be a zero placeholder while computing a draft identity.
QByteArray serializeDocument(Domain, const QJsonObject &, QString *error = nullptr);
// Local numeric geometry check only; never establishes calibration qualification.
bool validateDeviceModelGeometry(const QJsonObject &, QString *error = nullptr);
QByteArray encodeQuery(Domain domain, quint32 operation, quint64 requestId, QString *error = nullptr);
bool decodeState(const QByteArray &, Domain, State *, QString *error = nullptr);
bool decodeDocument(const QByteArray &, Domain, Document *, QString *error = nullptr);
bool documentMatchesState(const Document &, const State &);
bool decodePublication(const QByteArray &, Publication *, QString *error = nullptr);
bool decodeInputStatus(const QByteArray &, InputStatus *, QString *error = nullptr);
bool decodeResultDiagnostics(const QByteArray &, ResultDiagnostics *,
                             QString *error = nullptr);
bool resultDiagnosticsMatchesPublication(const ResultDiagnostics &,
                                         const PublicationIdentity &);
bool samePublication(const PublicationIdentity &, const PublicationIdentity &);
QByteArray encodeLogCatalogRequest(quint32 maximumEntries = 32, QString *error = nullptr);
bool decodeLogCatalog(const QByteArray &, LogCatalog *, QString *error = nullptr);
QByteArray encodeLogReadRequest(const LogEntry &, quint64 offset, quint32 maximumBytes, QString *error = nullptr);
// Chunk CRC protects the 64-byte header; outer USB payload CRC protects data.
bool decodeLogChunk(const QByteArray &, const LogEntry &, quint64 expectedOffset, quint32 maximumBytes, LogChunk *,
                    QString *error = nullptr);
} // namespace ucm::productv9
