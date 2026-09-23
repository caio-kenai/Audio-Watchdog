#pragma once
// Watchdog engine: periodically enumerates audio endpoints and enforces the
// exclusive-mode setting, reacting promptly to device-change notifications.
#ifndef AUDIOWATCHDOG_WATCHDOGENGINE_H
#define AUDIOWATCHDOG_WATCHDOGENGINE_H

#include "audio/AudioTypes.h"
#include "audio/ExclusiveModeManager.h"
#include "config/Config.h"

#include <atomic>
#include <functional>
#include <mutex>

namespace aw {

// Result summary of one full scan pass.
struct ScanReport {
    int endpointsScanned = 0;
    int fixed = 0;             // endpoints where exclusive mode was re-disabled
    int alreadyOff = 0;        // endpoints already compliant
    int failed = 0;            // endpoints that could not be enforced/read
    int skipped = 0;           // endpoints skipped because enforcement is off
};

class WatchdogEngine {
public:
    WatchdogEngine(IAudioDeviceLister& lister,
                   IExclusiveModeStore& store,
                   Config config);
    ~WatchdogEngine();

    WatchdogEngine(const WatchdogEngine&) = delete;
    WatchdogEngine& operator=(const WatchdogEngine&) = delete;

    // Starts the worker thread. Callbacks, when provided, are invoked on the
    // worker thread after each scan pass.
    bool Start(std::function<void(const ScanReport&)> onScan = nullptr);

    // Signals the worker to run a scan now and to stop on the next cycle.
    void RequestRescan();
    void Stop();

    // Runs one scan synchronously (also used by the `scan`/`diagnose` CLI).
    ScanReport ScanOnce();

    // Current config snapshot.
    Config GetConfig() const;

private:
    void WorkerMain();

    IAudioDeviceLister& lister_;
    IExclusiveModeStore& store_;
    ExclusiveModeManager manager_;
    Config config_;
    std::atomic<bool> running_{false};
    HANDLE stopEvent_ = nullptr;
    HANDLE rescanEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    std::function<void(const ScanReport&)> onScan_;
    mutable std::mutex configMutex_;
};

} // namespace aw

#endif // AUDIOWATCHDOG_WATCHDOGENGINE_H