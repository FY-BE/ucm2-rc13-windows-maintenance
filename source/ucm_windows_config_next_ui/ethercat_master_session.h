#pragma once

#include "ethercat_protocol.h"

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <functional>

namespace ucm::ethercat {

struct MasterRunOptions {
    QString adapterId;
    QByteArray sourceMac;
    MasterOutput output;
    int periodMs = 1;
    int cycles = 100;
    int opTimeoutMs = 9500;
    std::atomic_bool *stopRequested = nullptr;
    std::function<void(int, int, int, int, int, const SlaveInput &)> liveUpdate;
};

struct MasterRunResult {
    bool success = false;
    bool returnedToInit = false;
    QString status;
    QString profile;
    int inputBytes = 0;
    int alStatus = 0;
    int alStatusCode = 0;
    int completedCycles = 0;
    int droppedCycles = 0;
    int minimumWkc = 0;
    int maximumWkc = 0;
    SlaveInput lastInput;
    QJsonObject evidence;
};

class MasterSession final {
public:
    static MasterRunResult run(const MasterRunOptions &options);
};

} // namespace ucm::ethercat
