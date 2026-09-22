#pragma once
#include "usb_product_wire_v9.h"
#include "config_transport.h"
#include <functional>
#include <QHash>

namespace ucm::productv9 {
// The callback must perform a fresh device authority check for every operation.
// Returning true is not authorization to bypass the transport's authority gate.
using Transact = std::function<bool(quint16, const QByteArray &, QByteArray *, QString *)>;
using WriteGate = std::function<bool(QString *)>;
enum class OperationOutcome { Idle, Pending, Succeeded, Failed, Unresolved };
struct ConfigWriteRequest {
    Domain domain = Domain::DeviceModel;
    quint32 operation = 0; // 1 Apply, 2 SaveStartup, 3 Validate
    quint64 bootId=0, authorityGeneration=0, requestId=0, entryId=0, sessionId=0;
    // These are ARM monotonic timestamps; never use the Windows clock epoch.
    quint64 createdMonotonicNs=0, expiresMonotonicNs=0;
    QByteArray payloadDigest, expectedCurrentId, resultId, systemSha256, json;
};
QByteArray encodeConfigWrite(const ConfigWriteRequest &, QString *error=nullptr);
class ConfigOperationClient {
public:
    ConfigOperationClient(Transact transact, WriteGate authorityGate);
    bool start(const ConfigWriteRequest &, const UsbExtendedCapabilitiesV2 &,
               bool userAuthorized, bool commissioned, QString *error=nullptr);
    bool poll(quint64 queryRequestId, QString *error=nullptr);
    void disconnected();
    OperationOutcome outcome() const { return outcome_; }
    const State &state() const { return state_; }
private:
    bool accept(const QByteArray &, quint64 responseId, QString *);
    Transact transact_; WriteGate gate_; ConfigWriteRequest request_; State state_;
    OperationOutcome outcome_=OperationOutcome::Idle;
    quint64 lastTransitionSequence_=0;
    QHash<QByteArray,QByteArray> usedRequests_;
    QHash<QByteArray,quint64> usedEntries_;
};
struct UpgradePackage {
    quint64 transactionId=0, totalBytes=0;
    quint32 chunkBytes=65536, packageVersion=0;
    QByteArray sha256, deviceModel, buildId;
};
struct UpgradeStatus {
    quint32 state=0, result=0, packageVersion=0;
    quint64 transactionId=0, receivedBytes=0, totalBytes=0;
    QByteArray expectedSha256, actualSha256;
};
bool decodeUpgradeStatus(const QByteArray &, UpgradeStatus *, QString *error=nullptr);
class UpgradeClient {
public:
    UpgradeClient(Transact transact, WriteGate authorityGate);
    bool begin(const UpgradePackage &, const UsbExtendedCapabilitiesV2 &,
               bool userAuthorized, bool commissioned, QString *error=nullptr);
    bool chunk(const QByteArray &, QString *error=nullptr);
    bool finalize(QString *error=nullptr);
    bool poll(QString *error=nullptr);
    bool abort(quint32 reason, bool userAuthorized, QString *error=nullptr);
    // Activation is always separate from staging and requires explicit approval.
    bool activate(const UsbExtendedCapabilitiesV2 &, bool userAuthorized,
                  QString *error=nullptr);
    void disconnected();
    OperationOutcome outcome() const { return outcome_; }
    const UpgradeStatus &status() const { return status_; }
private:
    bool exchange(quint16, const QByteArray &, QString *);
    Transact transact_; WriteGate gate_; UpgradePackage package_; UpgradeStatus status_;
    OperationOutcome outcome_=OperationOutcome::Idle;
    bool commissioned_=false;
    // Once activation may have reached ARM, only same-transaction queries are
    // safe until an identity-bound terminal state is observed.
    bool activationAttempted_=false;
    UpgradeStatus lastConfirmedStatus_;
    QHash<quint64,QByteArray> usedTransactions_;
};
}
