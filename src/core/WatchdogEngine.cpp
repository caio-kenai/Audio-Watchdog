#include "core/WatchdogEngine.h"

#include "util/ComInitializer.h"
#include "util/StrUtil.h"
#include "logging/Logger.h"
#include "logging/EventLog.h"

#include <algorithm>
#include <exception>

namespace aw {

namespace {

// Endpoints whose last change failed are left alone by notification-triggered
// scans for this long; periodic/full scans still retry them.
constexpr ULONGLONG kFailureBackoffMs = 30000;
// Notifications usually arrive in bursts; wait for the burst to settle.
constexpr DWORD kDebounceMs = 400;
// Retry soon when the endpoint database is not reachable yet (early boot).
constexpr DWORD kEnumerationRetryMs = 5000;

const wchar_t* DeviceStateText(DWORD state) {
    switch (state) {
        case DEVICE_STATE_ACTIVE: return L"active";
        case DEVICE_STATE_DISABLED: return L"disabled";
        case DEVICE_STATE_NOTPRESENT: return L"not present";
        case DEVICE_STATE_UNPLUGGED: return L"unplugged";
        default: return L"unknown";
    }
}

} // namespace

std::wstring WatchdogStateName(WatchdogState s) {
    switch (s) {
        case WatchdogState::Running: return L"RUNNING";
        case WatchdogState::Paused: return L"PAUSED";
        case WatchdogState::Stopping: return L"STOPPING";
        case WatchdogState::Stopped: return L"STOPPED";
    }
    return L"UNKNOWN";
}

WatchdogEngine::WatchdogEngine(IAudioDeviceLister& lister,
                               IExclusiveModeStore& store,
                               IAudioFormatStore& formatStore,
                               Config config)
    : lister_(lister), manager_(store), formatManager_(formatStore), config_(std::move(config)) {
    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    rescanEvent_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WatchdogEngine::~WatchdogEngine() {
    Stop();
    if (stopEvent_) ::CloseHandle(stopEvent_);
    if (rescanEvent_) ::CloseHandle(rescanEvent_);
}

Config WatchdogEngine::GetConfig() const {
    std::lock_guard<std::mutex> lk(configMutex_);
    return config_;
}

void WatchdogEngine::SetConfig(const Config& config) {
    {
        std::lock_guard<std::mutex> lk(configMutex_);
        config_ = config;
    }
    std::lock_guard<std::mutex> lk(memoMutex_);
    noticed_.clear();
    failureTick_.clear();
}

bool WatchdogEngine::Start(std::function<void(const ScanReport&)> onScan) {
    if (thread_) return true;
    if (!stopEvent_ || !rescanEvent_) return false;
    onScan_ = std::move(onScan);
    ::ResetEvent(stopEvent_);
    state_ = WatchdogState::Running;
    thread_ = ::CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        static_cast<WatchdogEngine*>(p)->WorkerMain();
        return 0;
    }, this, 0, nullptr);
    if (!thread_) {
        state_ = WatchdogState::Stopped;
        return false;
    }
    return true;
}

void WatchdogEngine::RequestRescan() {
    if (rescanEvent_) ::SetEvent(rescanEvent_);
}

void WatchdogEngine::RequestFullScan() {
    fullScanPending_ = true;
    RequestRescan();
}

void WatchdogEngine::Stop() {
    if (!thread_) return;
    state_ = WatchdogState::Stopping;
    ::SetEvent(stopEvent_);
    // A scan pass is bounded (a handful of COM calls per endpoint), so the
    // worker always comes back; never kill it mid-call with TerminateThread.
    ::WaitForSingleObject(thread_, INFINITE);
    ::CloseHandle(thread_);
    thread_ = nullptr;
    state_ = WatchdogState::Stopped;
}

void WatchdogEngine::Pause() {
    WatchdogState expected = WatchdogState::Running;
    if (state_.compare_exchange_strong(expected, WatchdogState::Paused)) {
        Logger::Instance().Info(L"Monitoring PAUSED: no audio device will be modified until resumed.");
    }
}

void WatchdogEngine::Resume() {
    WatchdogState expected = WatchdogState::Paused;
    if (state_.compare_exchange_strong(expected, WatchdogState::Running)) {
        {
            std::lock_guard<std::mutex> lk(memoMutex_);
            noticed_.clear();
            failureTick_.clear();
        }
        Logger::Instance().Info(L"Monitoring RESUMED: running a full device scan.");
        RequestFullScan();
    }
}

void WatchdogEngine::OnDeviceChange(const DeviceChange& change) {
    Logger& log = Logger::Instance();
    switch (change.kind) {
        case DeviceChange::Kind::Added:
            log.Info(L"Device added: " + change.id);
            break;
        case DeviceChange::Kind::Removed:
            log.Info(L"Device removed: " + change.id);
            break;
        case DeviceChange::Kind::StateChanged:
            log.Info(FormatW(L"Device state changed to %s: %s",
                             DeviceStateText(change.newState), change.id.c_str()));
            break;
        case DeviceChange::Kind::DefaultChanged:
            log.Debug(L"Default device changed: " + change.id);
            break;
        case DeviceChange::Kind::PropertyChanged:
            // Frequent (our own writes trigger it too); just rescan.
            RequestRescan();
            return;
    }
    // A (re)created or re-plugged endpoint is a new situation: forget old
    // failures and notices so it is checked and reported afresh.
    {
        std::lock_guard<std::mutex> lk(memoMutex_);
        failureTick_.erase(change.id);
        for (auto it = noticed_.begin(); it != noticed_.end();) {
            it = it->first.find(change.id) != std::wstring::npos ? noticed_.erase(it) : std::next(it);
        }
    }
    RequestRescan();
}

bool WatchdogEngine::InBackoff(const std::wstring& id, ScanKind kind) {
    if (kind == ScanKind::Full) return false;
    std::lock_guard<std::mutex> lk(memoMutex_);
    auto it = failureTick_.find(id);
    return it != failureTick_.end() && ::GetTickCount64() - it->second < kFailureBackoffMs;
}

void WatchdogEngine::RecordFailure(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(memoMutex_);
    failureTick_[id] = ::GetTickCount64();
}

void WatchdogEngine::ClearFailure(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(memoMutex_);
    failureTick_.erase(id);
}

void WatchdogEngine::NoticeOnce(const std::wstring& key, const std::wstring& message) {
    bool first = false;
    {
        std::lock_guard<std::mutex> lk(memoMutex_);
        auto it = noticed_.find(key);
        first = it == noticed_.end() || it->second != message;
        noticed_[key] = message;
    }
    if (first) {
        Logger::Instance().Warn(message);
    } else {
        Logger::Instance().Debug(message);
    }
}

void WatchdogEngine::WorkerMain() {
    // The enumeration helpers create and release their COM objects within
    // each pass, so a plain MTA here is fine.
    aw::ComInitializer com(COINIT_MULTITHREADED);

    ScanKind kind = ScanKind::Full;
    HANDLE waits[] = { stopEvent_, rescanEvent_ };
    for (;;) {
        bool enumerationFailed = false;
        if (state_.load() == WatchdogState::Running) {
            try {
                const ScanReport rep = ScanOnce(kind);
                enumerationFailed = rep.failed > 0 && rep.endpointsScanned == 0;
                if (onScan_) onScan_(rep);
            } catch (const std::exception& e) {
                Logger::Instance().Error(L"Scan aborted by an exception: " + WideFromUtf8(e.what()));
            } catch (...) {
                Logger::Instance().Error(L"Scan aborted by an unknown exception.");
            }
        }

        const Config cfg = GetConfig();
        DWORD waitMs = cfg.checkIntervalSeconds * 1000u;
        if (enumerationFailed) waitMs = std::min(waitMs, kEnumerationRetryMs);

        const DWORD r = ::WaitForMultipleObjects(2, waits, FALSE, waitMs);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
        if (r == WAIT_OBJECT_0 + 1) {
            // Let a burst of notifications settle into a single pass.
            for (int i = 0; i < 10; ++i) {
                const DWORD d = ::WaitForMultipleObjects(2, waits, FALSE, kDebounceMs);
                if (d == WAIT_OBJECT_0) return;
                if (d != WAIT_OBJECT_0 + 1) break;
            }
            kind = fullScanPending_.exchange(false) ? ScanKind::Full : ScanKind::Triggered;
        } else {
            kind = ScanKind::Full; // periodic fallback
            fullScanPending_ = false;
        }
    }
}

ScanReport WatchdogEngine::ScanOnce(ScanKind kind) {
    ScanReport report;
    Logger& log = Logger::Instance();

    if (!CanModify()) {
        report.paused = true;
        return report;
    }

    const Config cfg = GetConfig();

    std::vector<EndpointInfo> endpoints;
    const HRESULT hr = lister_.Enumerate(endpoints);
    if (hr == E_NOTFOUND) {
        log.Debug(L"WatchdogEngine: no audio endpoints present.");
        return report;
    }
    if (FAILED(hr)) {
        log.Error(L"WatchdogEngine: enumeration failed: " + HrText(hr));
        report.failed = 1;
        return report;
    }

    for (EndpointInfo& info : endpoints) {
        if ((info.flow == EndpointFlow::Render && !cfg.monitorPlayback) ||
            (info.flow == EndpointFlow::Capture && !cfg.monitorCapture)) {
            continue;
        }
        // Pausing mid-pass stops further modifications immediately.
        if (!CanModify()) {
            report.paused = true;
            break;
        }

        ++report.endpointsScanned;
        const bool backoff = InBackoff(info.id, kind);
        const wchar_t* name = info.name.c_str();

        // --- Exclusive Mode Protection -----------------------------------
        if (cfg.exclusiveModeProtection) {
            const FixResult result = manager_.JudgeAndFix(info, cfg.enforce && !backoff && CanModify());
            switch (result) {
                case FixResult::Fixed:
                    ++report.fixed;
                    ClearFailure(info.id);
                    log.Warn(FormatW(L"[%s] exclusive mode was ENABLED -> forced off", name));
                    break;
                case FixResult::AlreadyOff:
                    ++report.alreadyOff;
                    break;
                case FixResult::Unknown:
                    ++report.failed;
                    NoticeOnce(L"excl|" + info.id,
                               FormatW(L"[%s] could not read exclusive-mode state (%s)",
                                       name, HrText(info.exclusiveReadHr).c_str()));
                    break;
                case FixResult::WriteFailed:
                    ++report.failed;
                    RecordFailure(info.id);
                    log.Error(FormatW(L"[%s] FAILED to disable exclusive mode", name));
                    break;
                case FixResult::VerifyFailed:
                    ++report.failed;
                    RecordFailure(info.id);
                    log.Error(FormatW(L"[%s] write did not stick (verify failed)", name));
                    break;
                case FixResult::Skipped:
                    ++report.skipped;
                    log.Debug(FormatW(L"[%s] exclusive mode enabled but not changed (%s)", name,
                                      backoff ? L"recent failure, retry on next full scan"
                                              : L"enforcement off, report only"));
                    break;
            }
        }

        // --- Audio Format Standardization (active endpoints only) ---------
        if (cfg.formatStandardization && info.deviceState == DEVICE_STATE_ACTIVE) {
            const std::wstring key = L"fmt|" + info.id;
            const std::wstring target = DescribeFormatTarget(cfg);
            const FormatOutcome out = formatManager_.JudgeAndApply(
                info.id, cfg.sampleRate, cfg.bitDepth, cfg.enforce && !backoff && CanModify());
            switch (out.result) {
                case FormatResult::Compliant:
                    ++report.formatCompliant;
                    break;
                case FormatResult::Applied:
                    ++report.formatApplied;
                    ClearFailure(info.id);
                    log.Info(FormatW(L"[%s] default format changed: %s -> %s", name,
                                     DescribeFormat(out.before).c_str(), target.c_str()));
                    break;
                case FormatResult::Unsupported:
                    ++report.formatUnsupported;
                    NoticeOnce(key, FormatW(L"[%s] format standardization skipped. Requested: %s. "
                                            L"Device supports: %s. Current: %s. "
                                            L"Reason: requested format is not supported.",
                                            name, target.c_str(), out.supported.c_str(),
                                            DescribeFormat(out.before).c_str()));
                    break;
                case FormatResult::Unknown:
                    ++report.formatFailed;
                    NoticeOnce(key, FormatW(L"[%s] format standardization skipped: the current or "
                                            L"supported formats could not be determined (%s).",
                                            name, HrText(out.hr).c_str()));
                    break;
                case FormatResult::WriteFailed:
                    ++report.formatFailed;
                    RecordFailure(info.id);
                    log.Error(FormatW(L"[%s] FAILED to set default format %s (%s)", name,
                                      target.c_str(), HrText(out.hr).c_str()));
                    break;
                case FormatResult::VerifyFailed:
                    ++report.formatFailed;
                    RecordFailure(info.id);
                    log.Error(FormatW(L"[%s] default format %s did not stick (verify failed)",
                                      name, target.c_str()));
                    break;
                case FormatResult::Skipped:
                    log.Debug(FormatW(L"[%s] format is %s, target %s not applied (%s)", name,
                                      DescribeFormat(out.before).c_str(), target.c_str(),
                                      backoff ? L"recent failure" : L"enforcement off"));
                    break;
            }
            if (out.result == FormatResult::Compliant || out.result == FormatResult::Applied) {
                std::lock_guard<std::mutex> lk(memoMutex_);
                noticed_.erase(key);
            }
        }
    }

    const bool changed = report.fixed > 0 || report.formatApplied > 0;
    const std::wstring summary = FormatW(
        L"Scan (%s): %d endpoint(s) | exclusive: %d fixed, %d ok | format: %d applied, %d ok, "
        L"%d unsupported | errors: %d",
        kind == ScanKind::Full ? L"full" : L"triggered", report.endpointsScanned, report.fixed,
        report.alreadyOff, report.formatApplied, report.formatCompliant, report.formatUnsupported,
        report.failed + report.formatFailed);
    if (changed) {
        log.Info(summary);
    } else {
        log.Debug(summary);
    }

    if (report.fixed > 0) {
        WriteEventLog(L"Audio Watchdog", EVENTLOG_WARNING_TYPE,
                      FormatW(L"Audio Watchdog re-disabled exclusive mode on %d endpoint(s).",
                              report.fixed));
    }
    if (report.formatApplied > 0) {
        WriteEventLog(L"Audio Watchdog", EVENTLOG_INFORMATION_TYPE,
                      FormatW(L"Audio Watchdog set the default format (%s) on %d endpoint(s).",
                              DescribeFormatTarget(cfg).c_str(), report.formatApplied));
    }
    return report;
}

} // namespace aw
