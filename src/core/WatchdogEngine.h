#pragma once
// Watchdog engine: periodically enumerates audio endpoints and enforces the
// exclusive-mode setting (and, optionally, a standard default format),
// reacting promptly to device-change notifications.
#ifndef AUDIOWATCHDOG_WATCHDOGENGINE_H
#define AUDIOWATCHDOG_WATCHDOGENGINE_H

#include "audio/AudioTypes.h"
#include "audio/AudioDeviceWatcher.h"
#include "audio/ExclusiveModeManager.h"
#include "audio/FormatManager.h"
#include "config/Config.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace aw {

// Lifecycle of the engine. Only RUNNING modifies devices.
enum class WatchdogState { Running, Paused, Stopping, Stopped };

std::wstring WatchdogStateName(WatchdogState s);

// Why a scan runs. Full scans always retry every endpoint; triggered scans
// (device notifications) skip endpoints that failed moments ago so a driver
// that rejects a change cannot cause a notification -> write -> notification
// loop.
enum class ScanKind { Full, Triggered };

// Result summary of one full scan pass.
struct ScanReport {
    int endpointsScanned = 0;
    int fixed = 0;             // endpoints where exclusive mode was re-disabled
    int alreadyOff = 0;        // endpoints already compliant
    int failed = 0;            // endpoints that could not be enforced/read
    int skipped = 0;           // endpoints skipped because enforcement is off
    int formatApplied = 0;     // endpoints switched to the target format
    int formatCompliant = 0;   // endpoints already at the target format
    int formatUnsupported = 0; // endpoints that cannot use the target format
    int formatFailed = 0;      // format could not be read/applied
    bool paused = false;       // scan aborted because monitoring is paused
};

class WatchdogEngine {
public:
    WatchdogEngine(IAudioDeviceLister& lister,
                   IExclusiveModeStore& store,
                   IAudioFormatStore& formatStore,
                   Config config);
    ~WatchdogEngine();

    WatchdogEngine(const WatchdogEngine&) = delete;
    WatchdogEngine& operator=(const WatchdogEngine&) = delete;

    // Starts the worker thread. Callbacks, when provided, are invoked on the
    // worker thread after each scan pass.
    bool Start(std::function<void(const ScanReport&)> onScan = nullptr);

    // Signals the worker to run a (triggered) scan soon.
    void RequestRescan();
    // Requests a full scan (all endpoints, ignoring the failure backoff).
    void RequestFullScan();
    // Stops and joins the worker. Idempotent.
    void Stop();

    // Suspends every modification. Device events are still received.
    void Pause();
    // Leaves PAUSED and runs a full scan.
    void Resume();
    WatchdogState State() const { return state_.load(); }

    // Feeds a watcher notification: logs it and schedules a rescan.
    void OnDeviceChange(const DeviceChange& change);

    // Runs one scan synchronously (also used by the `scan` CLI and tests).
    ScanReport ScanOnce(ScanKind kind = ScanKind::Full);

    Config GetConfig() const;
    // Replaces the configuration (takes effect on the next scan).
    void SetConfig(const Config& config);

private:
    void WorkerMain();
    bool CanModify() const { return state_.load() == WatchdogState::Running; }
    bool InBackoff(const std::wstring& id, ScanKind kind);
    void RecordFailure(const std::wstring& id);
    void ClearFailure(const std::wstring& id);
    // Logs `message` at warning level the first time (per endpoint and
    // topic), then at debug level until the endpoint changes.
    void NoticeOnce(const std::wstring& key, const std::wstring& message);

    IAudioDeviceLister& lister_;
    ExclusiveModeManager manager_;
    FormatManager formatManager_;
    Config config_;
    std::atomic<WatchdogState> state_{WatchdogState::Running};
    std::atomic<bool> fullScanPending_{false};
    HANDLE stopEvent_ = nullptr;
    HANDLE rescanEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    std::function<void(const ScanReport&)> onScan_;
    mutable std::mutex configMutex_;

    std::mutex memoMutex_;
    std::map<std::wstring, ULONGLONG> failureTick_;   // endpoint id -> last failure
    std::map<std::wstring, std::wstring> noticed_;    // key -> last logged notice
};

} // namespace aw

#endif // AUDIOWATCHDOG_WATCHDOGENGINE_H
