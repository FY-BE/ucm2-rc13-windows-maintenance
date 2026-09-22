#pragma once

#include <QByteArray>
#include <QString>

namespace ucm::ethercat {

inline constexpr quint32 kInvalidForceKn = 0xffffffffU;

enum class Command : quint8 {
    Aprd = 0x01,
    Apwr = 0x02,
    Fprd = 0x04,
    Fpwr = 0x05,
    Brd = 0x07,
    Lrd = 0x0a,
    Lwr = 0x0b,
    Lrw = 0x0c
};

struct DatagramReply {
    bool valid = false;
    QByteArray data;
    quint16 workingCounter = 0;
    QString error;
};

QByteArray makeFrame(const QByteArray &sourceMac, Command command, quint8 index,
                     quint16 adp, quint16 ado, const QByteArray &data);
DatagramReply parseReply(const QByteArray &frame, Command command, quint8 index,
                         int dataLength);
QString alStatusCodeText(quint16 code);

struct MasterOutput {
    quint16 machineModel = 1;
    quint64 currentUtcMs = 0;
    quint32 clampingForceSetpointKn = 0;
    /* FQX wire value in 0.1 mm units; 662 mm is encoded as 6620. */
    quint16 moldThickness = 6620;
    quint8 machineState = 4;
    quint64 deviceEnabledUtcMs = 0;
    quint64 accumulatedRuntimeMs = 0;
};

struct SlaveInput {
    quint64 dataTimestampUtcMs = 0;
    quint32 tieBar1ForceKn = 0;
    quint32 tieBar2ForceKn = 0;
    quint32 tieBar3ForceKn = 0;
    quint32 tieBar4ForceKn = 0;
    quint32 totalClampingForceKn = 0;
    quint16 loadImbalanceRate = 0;
    quint8 faultFlag = 0;
    quint16 equipmentErrorCode = 0;
    quint16 configurationErrorCode = 0;

    bool rodForcesValid() const;
    quint64 windowsRodSumKn() const;
};

QByteArray encodeMasterOutput(const MasterOutput &output);
bool decodeSlaveInput(const QByteArray &bytes, SlaveInput *input,
                      QString *error = nullptr);

} // namespace ucm::ethercat
