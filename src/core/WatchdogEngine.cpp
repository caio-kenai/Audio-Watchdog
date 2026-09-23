#include "core/WatchdogEngine.h"

#include "util/ComInitializer.h"
#include "util/StrUtil.h"
#include "logging/Logger.h"
#include "logging/EventLog.h"

#include <algorithm>

namespace aw {

WatchdogEngine::WatchdogEngine(IAudioDeviceLister& lister,
                               IExclusiveModeStore& store,
                               Config config)
    : lister_(lister), store_(store), manager_(store), config_(std::move(config)) {
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

bool WatchdogEngine::Start(std::function<void(const ScanReport&)> onScan) {
    if (running_) return true;
    onScan_ = std::move(onScan);
    running_ = true;
    thread_ = ::CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        static_cast<WatchdogEngine*>(p)->WorkerMain();
        return 0;
    }, this, 0, nullptr);
    if (!thread_) {
        running_ = false;
        return false;
    }
    return true;
}

void WatchdogEngine::RequestRescan() {
    if (rescanEvent_) ::SetEvent(rescanEvent_);
}

void WatchdogEngine::Stop() {
    running_ = false;
    if (stopEvent_) ::SetEvent(stopEvent_);
    if (thread_) {
        if (::WaitForSingleObject(thread_, 10000) == WAIT_TIMEOUT) {
            ::TerminateThread(thread_, 0);
        }
        ::CloseHandle(thread_);
        thread_ = nullptr;
    }
}

void WatchdogEngine::WorkerMain() {
    // COM is initialized per call; the enumeration helpers create and release
    // their objects within each pass, so a plain MTA here is fine.
    aw::ComInitializer com(COINIT_MULTITHREADED);

    const std::uint32_t intervalSec = config_.checkIntervalSeconds;
    const DWORD intervalMs = static_cast<DWORD>(intervalSec) * 1000u;

    // Initial pass immediately after startup.
    ScanReport first = ScanOnce();
    if (onScan_) onScan_(first);

    HANDLE waits[] = { stopEvent_, rescanEvent_ };
    while (running_) {
        const DWORD r = ::WaitForMultipleObjects(2, waits, FALSE, intervalMs);
        if (r == WAIT_OBJECT_0) break;                      // stop requested
        if (r == WAIT_OBJECT_0 + 1) { /* rescan requested, run now */ }
        const ScanReport rep = ScanOnce();
        if (onScan_) onScan_(rep);
    }
}

ScanReport WatchdogEngine::ScanOnce() {
    ScanReport report;

    Config cfg = GetConfig();
    if (cfg.exclusiveDisabled) {
        // Pure monitor mode: report states but never mutate.
    }

    std::vector<EndpointInfo> endpoints;
    HRESULT hr = lister_.Enumerate(endpoints);
    if (FAILED(hr)) {
        Logger::Instance().Error(L"WatchdogEngine: enumeration failed: " + HrText(hr));
        report.failed = 1;
        return report;
    }

    for (EndpointInfo& info : endpoints) {
        if ((info.flow == EndpointFlow::Render && !cfg.monitorPlayback) ||
            (info.flow == EndpointFlow::Capture && !cfg.monitorCapture)) {
            continue;
        }

        ++report.endpointsScanned;

        const FixResult result = manager_.JudgeAndFix(info, cfg.enforce);
        switch (result) {
            case FixResult::Fixed:
                ++report.fixed;
                Logger::Instance().Warn(
                    FormatW(L"[%s] exclusive mode was ENABLED -> forced off",
                            info.name.c_str()));
                break;
            case FixResult::AlreadyOff:
                ++report.alreadyOff;
                break;
            case FixResult::Unknown:
                ++report.failed;
                Logger::Instance().Warn(
                    FormatW(L"[%s] could not read exclusive-mode state (%s)",
                            info.name.c_str(), HrText(info.exclusiveReadHr).c_str()));
                break;
            case FixResult::WriteFailed:
                ++report.failed;
                Logger::Instance().Error(
                    FormatW(L"[%s] FAILED to disable exclusive mode",
                            info.name.c_str()));
                break;
            case FixResult::VerifyFailed:
                ++report.failed;
                Logger::Instance().Error(
                    FormatW(L"[%s] write did not stick (verify failed)",
                            info.name.c_str()));
                break;
            case FixResult::Skipped:
                ++report.skipped;
                Logger::Instance().Debug(
                    FormatW(L"[%s] exclusive mode enabled but enforcement off (report only)",
                            info.name.c_str()));
                break;
        }
    }

    if (report.fixed > 0) {
        WriteEventLog(L"Audio Watchdog", EVENTLOG_WARNING_TYPE,
                      FormatW(L"Audio Watchdog re-disabled exclusive mode on %d endpoint(s).",
                              report.fixed));
    }
    return report;
}

} // namespace aw