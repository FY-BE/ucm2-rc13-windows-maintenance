#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <array>

namespace ucm {

constexpr int kUsbRuntimeProgressBytesV9 = 256;
constexpr int kUsbRuntimeActionBytesV9 = 64;
constexpr int kUsbRuntimeActionReceiptBytesV9 = 128;

struct UsbAgcPointV9 {
    std::array<quint32, 4> lnaDb {};
    quint32 pgaDb = 24;
    quint32 vcntlCode = 0;
    quint32 nominalHvV = 0;
    quint32 burstCycles = 0;
};

struct UsbRuntimeProgressV9 {
    bool available = false;
    quint64 generation = 0;
    quint64 updatedMonotonicNs = 0;
    quint64 stageElapsedMs = 0;
    quint64 stageLimitMs = 0;
    quint64 estimatedRemainingMs = 0;
    quint64 lastActionTransactionId = 0;
    quint32 stage = 0;
    quint32 stageItem = 0;
    quint32 stageItemCount = 0;
    quint32 overallPermille = 0;
    quint32 waitReason = 0;
    bool canMeasure = false;
    quint32 qualityLevel = 0;
    quint32 plcState = 0;
    bool plcFresh = false;
    quint32 templateValidMask = 0;
    quint32 tareState = 0;
    quint32 tareGeneration = 0;
    quint32 lastAction = 0;
    qint32 lastActionResult = 0;
    quint32 faultCode = 0;
    UsbAgcPointV9 current;
    UsbAgcPointV9 best;
    quint32 activeDeviceModelId = 0;
    quint32 startupDeviceModelId = 0;
    quint32 pendingDeviceModelId = 0;
    quint32 modelSwitchState = 0;
    qint32 modelSwitchResult = 0;
    quint32 modelProfileOrigin = 0;
    quint64 modelSwitchTransactionId = 0;
};

struct UsbRuntimeProgressResultV9 {
    bool success = false;
    QString message;
    UsbRuntimeProgressV9 progress;
    QByteArray raw;
};

struct UsbRuntimeActionResultV9 {
    bool success = false;
    QString message;
    quint32 action = 0;
    qint32 result = 0;
    quint64 transactionId = 0;
    quint64 generation = 0;
    quint32 stage = 0;
    quint32 tareState = 0;
    quint32 tareGeneration = 0;
    quint32 faultCode = 0;
    QByteArray raw;
};

bool decodeUsbRuntimeProgressV9(const QByteArray &payload,
                                UsbRuntimeProgressV9 *progress,
                                QString *error);
QByteArray encodeUsbRuntimeActionV9(quint32 action, quint64 transactionId,
                                    quint64 expectedGeneration,
                                    QString *error);
bool decodeUsbRuntimeActionReceiptV9(const QByteArray &payload,
                                     UsbRuntimeActionResultV9 *receipt,
                                     QString *error);
QString usbRuntimeStageTextV9(quint32 stage);
QString usbRuntimeQualityTextV9(quint32 quality);

} // namespace ucm
