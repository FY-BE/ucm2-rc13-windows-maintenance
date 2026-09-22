#pragma once

#include <QtGlobal>

namespace ucm::ui {

constexpr int telemetryIntervalMs = 20;
constexpr int runtimeStatusIntervalMs = 500;
constexpr int productStateIntervalMs = 5000;
constexpr int runtimeConfigIntervalMs = 1000;
constexpr int waveformIntervalMs = 1000;
constexpr int realtimeTrendPage = 1;

constexpr int connectionRetryDelayMs(int attempt)
{
    return attempt <= 0 ? 1000 : attempt == 1 ? 2000 : 5000;
}

// Skip missed ticks: callers keep one request in flight and never enqueue catch-up polls.
constexpr bool telemetryDue(bool connected, bool busy, qint64 nowMs, qint64 lastPollMs)
{
    return connected && !busy && (lastPollMs < 0 || (nowMs >= lastPollMs
        && nowMs - lastPollMs >= telemetryIntervalMs));
}

inline bool waveformDue(int activePage, qint64 nowMs,
                        qint64 lastWaveformMs)
{
    return activePage == realtimeTrendPage
        && nowMs - lastWaveformMs >= waveformIntervalMs;
}

} // namespace ucm::ui
