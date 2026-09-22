#pragma once

#include "config_transport.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace ucm {

constexpr quint32 kUsbWireMagicV1 = 0x31425355U;
constexpr quint16 kUsbWireAbiV1 = 1U;
constexpr int kUsbWireHeaderBytesV1 = 40;
constexpr quint32 kUsbWirePayloadMaximumV1 = 262144U;
constexpr quint64 kUsbConfigFieldsAllV1 = 0x3FFFFULL;

enum class UsbMessageTypeV1 : quint16 {
    Capabilities = 1,
    Telemetry = 4,
    LogList = 5,
    LogRead = 6,
    Waveform = 7,
    Ping = 8,
    ExtendedCapabilitiesV2 = 16,
    ParameterCatalogV2 = 17,
    ConfigStateV2 = 18,
    ConfigApplyV2 = 19,
    AuthorityStateV2 = 20,
    AuthorityCommandV2 = 21,
    RuntimeStatusV2 = 22,
    CompositeRuntimeStatusV1 = 23,
    DeviceModelQueryStateV2 = 24,
    DeviceModelQueryActiveDocumentV2 = 25,
    DeviceModelQueryStartupDocumentV2 = 26,
    DeviceModelValidateV2 = 27,
    DeviceModelApplyV2 = 28,
    DeviceModelSaveStartupV2 = 29,
    ResultSnapshotV1 = 30,
    ResultDiagnosticsV1 = 31,
    UpgradeBeginV2 = 32,
    UpgradeChunkV2 = 33,
    UpgradeFinalizeV2 = 34,
    UpgradeStateV2 = 35,
    UpgradeAbortV2 = 36,
    LogCatalogV2 = 37,
    LogReadV2 = 38,
    SystemInputPolicyQueryStateV1 = 39,
    SystemInputPolicyQueryActiveDocumentV1 = 40,
    SystemInputPolicyQueryStartupDocumentV1 = 41,
    SystemInputPolicyValidateV1 = 42,
    SystemInputPolicyApplyV1 = 43,
    SystemInputPolicySaveStartupV1 = 44,
    ResultInputStatusV1 = 45,
    UpgradeActivateV2 = 46,
    RuntimeActionV1 = 47,
    RuntimeProgressV1 = 48,
    Error = 255
};

struct UsbFrameV1 {
    quint16 messageType = 0;
    quint16 flags = 0;
    quint64 sequence = 0;
    quint64 transactionId = 0;
    QByteArray payload;
};

struct UsbCapabilitiesV1 {
    quint64 configFieldMask = 0;
    quint64 logSourceMask = 0;
    quint32 waveformFlags = 0;
    quint16 vendorId = 0;
    quint16 productId = 0;
    quint8 outEndpoint = 0;
    quint8 inEndpoint = 0;
    QString buildId;
};

struct UsbConfigReceiptV1 {
    quint32 kind = 0;
    qint32 result = 0;
    quint64 transactionId = 0;
    quint64 baseGeneration = 0;
    quint64 requestedGeneration = 0;
    quint64 activeGeneration = 0;
    quint64 changedFieldMask = 0;
    Configuration activeConfiguration;
    QByteArray candidateSha256;
    QByteArray hardwareReceipt;
    bool hardwareReceiptValid = false;
    std::array<qint32, 18> fieldResults {};
    HardwareConfigReceipt hardware;
};

quint32 usbCrc32V1(const QByteArray &bytes);
QString usbWireStatusTextV1(qint32 status);

QByteArray encodeUsbFrameV1(UsbMessageTypeV1 type, quint16 flags,
                            quint64 sequence, quint64 transactionId,
                            const QByteArray &payload, QString *error);

bool decodeUsbFrameHeaderV1(const QByteArray &header,
                            UsbFrameV1 *frame, quint32 *payloadBytes,
                            quint32 *payloadCrc32, QString *error);

bool finishUsbFrameV1(UsbFrameV1 *frame, const QByteArray &payload,
                      quint32 expectedPayloadBytes,
                      quint32 expectedPayloadCrc32, QString *error);

quint64 usbConfigurationChangedMaskV1(const Configuration &baseline,
                                      const Configuration &candidate);

QByteArray encodeUsbConfigurationV1(const Configuration &configuration,
                                    quint64 transactionId,
                                    quint64 baseGeneration,
                                    quint64 changedFieldMask,
                                    QString *error);

bool decodeUsbCapabilitiesV1(const QByteArray &payload,
                             UsbCapabilitiesV1 *capabilities,
                             QString *error);
bool decodeUsbProductCapabilitiesV9(const QByteArray &payload,
                                    UsbCapabilitiesV1 *capabilities,
                                    QString *error);

bool decodeUsbConfigReceiptV1(const QByteArray &payload,
                              UsbConfigReceiptV1 *receipt,
                              QString *error);

bool usbConfigurationValuesEqualV1(const Configuration &left,
                                   const Configuration &right);

bool decodeUsbTelemetrySnapshotV1(const QByteArray &payload,
                                  TelemetrySnapshot *snapshot,
                                  QString *error);
bool decodeUsbLogListV1(const QByteArray &payload,
                        DeviceLogListResult *result, QString *error);
bool decodeUsbLogChunkV1(const QByteArray &payload, quint32 sourceId,
                         quint64 offset, quint64 snapshotId,
                         DeviceLogChunkResult *result, QString *error);

} // namespace ucm
