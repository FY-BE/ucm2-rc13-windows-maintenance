#include "poll_schedule.h"

#include <cstdio>

int main()
{
    bool passed = true;
    const auto expect = [&passed](bool value, const char *message) {
        if (value) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        passed = false;
    };
    expect(ucm::ui::telemetryIntervalMs == 20,
           "telemetry target cadence must be 50 Hz");
    expect(ucm::ui::runtimeStatusIntervalMs == 500,
           "runtime status cadence must be 500 ms");
    expect(ucm::ui::productStateIntervalMs == 5000,
           "product state cadence must be 5 seconds");
    expect(!ucm::ui::telemetryDue(false, false, 100, 0), "disconnected must not poll");
    expect(!ucm::ui::telemetryDue(true, true, 1000, 0), "busy must skip ticks without accumulating requests");
    expect(!ucm::ui::telemetryDue(true, false, 19, 0), "never exceed 50 Hz");
    expect(ucm::ui::telemetryDue(true, false, 20, 0), "20 ms is due");
    expect(ucm::ui::telemetryDue(true, false, 1000, 0), "after a delay request only one current snapshot");
    expect(!ucm::ui::telemetryDue(true, false, 1000, 1000), "no catch-up request at same timestamp");
    expect(!ucm::ui::telemetryDue(true, false, 0, 20), "clock rollback cannot cause burst");
    expect(ucm::ui::telemetryDue(true, false, 0, -1), "first connected poll is immediate");
    expect(ucm::ui::waveformIntervalMs == 1000,
           "waveform display cadence must be exactly 1 Hz");
    expect(!ucm::ui::waveformDue(0, 5000, 0),
           "overview must not fetch the 2048x4 raw waveform");
    expect(!ucm::ui::waveformDue(2, 5000, 0),
           "diagnostics must not fetch the raw waveform in the background");
    expect(!ucm::ui::waveformDue(1, 1999, 1000),
           "trend page must not fetch before one second elapsed");
    expect(ucm::ui::waveformDue(1, 2000, 1000),
           "trend page must fetch once one second elapsed");
    expect(ucm::ui::connectionRetryDelayMs(0) == 1000 && ucm::ui::connectionRetryDelayMs(1) == 2000
        && ucm::ui::connectionRetryDelayMs(2) == 5000 && ucm::ui::connectionRetryDelayMs(100) == 5000,
        "USB reconnect must back off 1/2/5 seconds and stay bounded");
    return passed ? 0 : 1;
}
