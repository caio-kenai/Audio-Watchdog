#include "service/AudioWatchdogService.h"

#include "util/Paths.h"
#include "util/StrUtil.h"
#include "util/ServiceUtil.h"
#include "version.h"
#include "config/Config.h"
#include "logging/Logger.h"
#include "logging/EventLog.h"
#include "audio/AudioFormatStore.h"
#include "audio/CoreAudioDeviceLister.h"
#include "audio/ExclusiveModeStore.h"
#include "audio/AudioDeviceWatcher.h"
#include "core/WatchdogEngine.h"

#include <exception>
#include <mutex>

namespace aw {

namespace {

constexpr wchar_t kEventSource[] = L"Audio Watchdog";

// SCM bookkeeping. The control handler runs on the dispatcher thread while
// ServiceMain runs on its own thread, so every status change is serialized.
std::mutex             g_StatusMutex;
SERVICE_STATUS         g_ServiceStatus{};
SERVICE_STATUS_HANDLE  g_ServiceStatusHandle = nullptr;
DWORD                  g_CheckPoint = 1;

// Events consumed by the service thread. The handler only signals them.
HANDLE                 g_StopEvent = nullptr;
HANDLE                 g_PauseEvent = nullptr;
HANDLE                 g_ContinueEvent = nullptr;
HANDLE                 g_ReloadEvent = nullptr;

void ReportState(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHintMs = 0) {
    std::lock_guard<std::mutex> lk(g_StatusMutex);
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = state;
    g_ServiceStatus.dwControlsAccepted = 0;
    if (state == SERVICE_RUNNING || state == SERVICE_PAUSED) {
        g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN |
                                             SERVICE_ACCEPT_PAUSE_CONTINUE |
                                             SERVICE_ACCEPT_PARAMCHANGE;
    }
    g_ServiceStatus.dwWin32ExitCode = exitCode;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    const bool pending = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ||
                         state == SERVICE_PAUSE_PENDING || state == SERVICE_CONTINUE_PENDING;
    g_ServiceStatus.dwCheckPoint = pending ? g_CheckPoint++ : 0;
    if (!pending) g_CheckPoint = 1;
    g_ServiceStatus.dwWaitHint = waitHintMs;
    if (g_ServiceStatusHandle) ::SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
}

DWORD CurrentState() {
    std::lock_guard<std::mutex> lk(g_StatusMutex);
    return g_ServiceStatus.dwCurrentState;
}

void ConfigureLogging(const Config& cfg, bool consoleMode) {
    EnsureLogsDir();
    Logger& logger = Logger::Instance();
    logger.SetLevel(cfg.logLevel);
    if (cfg.enableLogging || consoleMode) {
        logger.Configure(cfg.enableLogging ? LogFilePath() : L"", cfg.logLevel, consoleMode);
    }
}

void LogFeatures(const Config& cfg) {
    Logger& logger = Logger::Instance();
    logger.Info(std::wstring(L"Exclusive Mode Protection: ") +
                (cfg.exclusiveModeProtection ? L"ENABLED" : L"DISABLED"));
    logger.Info(std::wstring(L"Audio Format Standardization: ") +
                (cfg.formatStandardization ? L"ENABLED (target " + DescribeFormatTarget(cfg) + L")"
                                           : L"DISABLED"));
    logger.Info(FormatW(L"Playback: %s | Capture: %s | Check interval: %us | Enforce: %s",
                        cfg.monitorPlayback ? L"on" : L"off", cfg.monitorCapture ? L"on" : L"off",
                        cfg.checkIntervalSeconds, cfg.enforce ? L"on" : L"report only"));
}

// Runs the watchdog until the stop event is signalled. Returns the Win32 exit
// code to report (non-zero makes the SCM apply the recovery actions).
DWORD RunCore(bool consoleMode) {
    EnsureProgramDataDirs();
    const Config cfg = LoadConfig(ConfigFilePath());
    ConfigureLogging(cfg, consoleMode);
    Logger& logger = Logger::Instance();
    logger.Info(L"Audio Watchdog " AWW_VERSION_WSTR L" starting (pid " +
                std::to_wstring(::GetCurrentProcessId()) + L").");
    LogFeatures(cfg);

    CoreAudioDeviceLister lister;
    ExclusiveModeStore store;
    AudioFormatStore formatStore;
    WatchdogEngine engine(lister, store, formatStore, cfg);

    if (!engine.Start()) {
        logger.Error(L"Could not start the watchdog worker thread (" +
                     FormatSystemError(::GetLastError()) + L").");
        return ERROR_SERVICE_SPECIFIC_ERROR;
    }

    AudioDeviceWatcher watcher;
    if (!watcher.Start([&engine](const DeviceChange& change) { engine.OnDeviceChange(change); })) {
        logger.Warn(L"Device notifications unavailable; relying on periodic checks.");
    }

    ReportState(SERVICE_RUNNING);
    logger.Info(L"Audio Watchdog is running.");
    if (!consoleMode) {
        WriteEventLog(kEventSource, EVENTLOG_INFORMATION_TYPE,
                      L"Audio Watchdog " AWW_VERSION_WSTR L" started.");
    }

    HANDLE waits[] = { g_StopEvent, g_PauseEvent, g_ContinueEvent, g_ReloadEvent };
    for (;;) {
        const DWORD r = ::WaitForMultipleObjects(4, waits, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
        if (r == WAIT_OBJECT_0 + 1) {
            engine.Pause();
            ReportState(SERVICE_PAUSED);
        } else if (r == WAIT_OBJECT_0 + 2) {
            // Pick up configuration edits, then leave PAUSED with a full scan.
            engine.SetConfig(LoadConfig(ConfigFilePath(), false));
            engine.Resume();
            ReportState(SERVICE_RUNNING);
        } else if (r == WAIT_OBJECT_0 + 3) {
            const Config fresh = LoadConfig(ConfigFilePath(), false);
            logger.SetLevel(fresh.logLevel);
            engine.SetConfig(fresh);
            logger.Info(L"Configuration reloaded.");
            LogFeatures(fresh);
            engine.RequestFullScan();
        }
    }

    logger.Info(L"Audio Watchdog stopping.");
    watcher.Stop();
    engine.Stop();
    logger.Info(L"Audio Watchdog stopped.");
    logger.Flush();
    if (!consoleMode) {
        WriteEventLog(kEventSource, EVENTLOG_INFORMATION_TYPE, L"Audio Watchdog stopped.");
    }
    return NO_ERROR;
}

DWORD RunCoreGuarded(bool consoleMode) {
    try {
        return RunCore(consoleMode);
    } catch (const std::exception& e) {
        const std::wstring what = WideFromUtf8(e.what());
        Logger::Instance().Error(L"Fatal error: " + what);
        WriteEventLog(kEventSource, EVENTLOG_ERROR_TYPE, L"Audio Watchdog stopped by a fatal error: " + what);
    } catch (...) {
        Logger::Instance().Error(L"Fatal error: unknown exception.");
        WriteEventLog(kEventSource, EVENTLOG_ERROR_TYPE, L"Audio Watchdog stopped by an unknown fatal error.");
    }
    Logger::Instance().Flush();
    return ERROR_SERVICE_SPECIFIC_ERROR;
}

DWORD WINAPI ServiceCtrlHandlerEx(DWORD control, DWORD /*eventType*/, LPVOID /*eventData*/, LPVOID /*context*/) {
    const DWORD state = CurrentState();
    const bool active = state == SERVICE_RUNNING || state == SERVICE_PAUSED;
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_PRESHUTDOWN:
            if (active) {
                ReportState(SERVICE_STOP_PENDING, NO_ERROR, 15000);
                ::SetEvent(g_StopEvent);
            }
            return NO_ERROR;
        case SERVICE_CONTROL_PAUSE:
            if (state == SERVICE_RUNNING) {
                ReportState(SERVICE_PAUSE_PENDING, NO_ERROR, 5000);
                ::SetEvent(g_PauseEvent);
            }
            return NO_ERROR;
        case SERVICE_CONTROL_CONTINUE:
            if (state == SERVICE_PAUSED) {
                ReportState(SERVICE_CONTINUE_PENDING, NO_ERROR, 5000);
                ::SetEvent(g_ContinueEvent);
            }
            return NO_ERROR;
        case SERVICE_CONTROL_PARAMCHANGE:
            if (active) ::SetEvent(g_ReloadEvent);
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

bool CreateEvents() {
    g_StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_PauseEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_ContinueEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_ReloadEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return g_StopEvent && g_PauseEvent && g_ContinueEvent && g_ReloadEvent;
}

void CloseEvents() {
    for (HANDLE* h : { &g_StopEvent, &g_PauseEvent, &g_ContinueEvent, &g_ReloadEvent }) {
        if (*h) ::CloseHandle(*h);
        *h = nullptr;
    }
}

void WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
    g_ServiceStatusHandle = ::RegisterServiceCtrlHandlerExW(kServiceName, ServiceCtrlHandlerEx, nullptr);
    if (!g_ServiceStatusHandle) return;

    if (!CreateEvents()) {
        CloseEvents();
        ReportState(SERVICE_STOPPED, ERROR_NOT_ENOUGH_MEMORY);
        return;
    }

    ReportState(SERVICE_START_PENDING, NO_ERROR, 15000);
    const DWORD exitCode = RunCoreGuarded(false);
    // Close the events before the final report: once SERVICE_STOPPED is
    // reported the process may be torn down at any moment.
    CloseEvents();
    ReportState(SERVICE_STOPPED, exitCode);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_SHUTDOWN_EVENT) {
        if (g_StopEvent) ::SetEvent(g_StopEvent);
        return TRUE;
    }
    return FALSE;
}

} // namespace

void RunAsService() {
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr },
    };
    if (!::StartServiceCtrlDispatcherW(table)) {
        if (::GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            Logger::Instance().Error(L"Not started by SCM; run in a console with --foreground, or install first.");
        }
    }
}

void RunForeground() {
    if (!CreateEvents()) {
        CloseEvents();
        return;
    }
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    Logger::Instance().Info(L"Running in foreground. Press Ctrl+C to stop.");
    RunCoreGuarded(true);
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    CloseEvents();
}

} // namespace aw
