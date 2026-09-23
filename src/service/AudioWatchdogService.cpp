#include "service/AudioWatchdogService.h"

#include "util/Paths.h"
#include "util/StrUtil.h"
#include "util/ComInitializer.h"
#include "config/Config.h"
#include "logging/Logger.h"
#include "logging/EventLog.h"
#include "audio/CoreAudioDeviceLister.h"
#include "audio/ExclusiveModeStore.h"
#include "audio/AudioDeviceWatcher.h"
#include "core/WatchdogEngine.h"

#include <thread>
#include <atomic>

namespace aw {

namespace {

SERVICE_STATUS         g_ServiceStatus{};
SERVICE_STATUS_HANDLE  g_ServiceStatusHandle = nullptr;
HANDLE                 g_StopEvent = nullptr;
WatchdogEngine*        g_Engine = nullptr;
AudioDeviceWatcher*    g_Watcher = nullptr;
std::atomic<bool>      g_ReportStopPending{false};

void ReportState(DWORD state, DWORD waitHintMs = 0) {
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = state;
    g_ServiceStatus.dwControlsAccepted = 0;
    if (state == SERVICE_RUNNING) {
        g_ServiceStatus.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_PARAMCHANGE;
    }
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = waitHintMs;
    ::SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
}

void SetupRuntime(Config& cfg, Logger& logger, bool consoleMode) {
    EnsureProgramDataDirs();
    if (cfg.enableLogging) {
        logger.Configure(LogFilePath(), cfg.logLevel, consoleMode);
    }
    logger.Info(L"Audio Watchdog starting (configured CheckIntervalSeconds=" +
                std::to_wstring(cfg.checkIntervalSeconds) + L")");
}

int RunCore() {
    Config cfg = LoadConfig(ConfigFilePath());

    Logger& logger = Logger::Instance();
    SetupRuntime(cfg, logger, false);

    CoreAudioDeviceLister lister;
    ExclusiveModeStore store;
    WatchdogEngine engine(lister, store, cfg);

    // Re-read the config on every scan so edits take effect without a service
    // restart. (The engine already guards config access with a mutex; here we
    // just rebuild the config snapshot at startup.)
    engine.Start();

    AudioDeviceWatcher watcher;
    watcher.Start([&]() {
        Logger::Instance().Info(L"Device-change notification received");
        engine.RequestRescan();
    });

    g_Engine = &engine;
    g_Watcher = &watcher;

    ReportState(SERVICE_RUNNING);
    logger.Info(L"Audio Watchdog is running.");

    // Block until SCM asks us to stop.
    ::WaitForSingleObject(g_StopEvent, INFINITE);

    logger.Info(L"Audio Watchdog stopping.");
    watcher.Stop();
    engine.Stop();
    g_Engine = nullptr;
    g_Watcher = nullptr;

    ReportState(SERVICE_STOPPED);
    logger.Info(L"Audio Watchdog stopped.");
    logger.Flush();
    return 0;
}

DWORD WINAPI ServiceCtrlHandlerEx(DWORD control, DWORD /*eventType*/, LPVOID /*eventData*/, LPVOID /*context*/) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_PRESHUTDOWN:
            if (g_ServiceStatus.dwCurrentState == SERVICE_RUNNING) {
                ReportState(SERVICE_STOP_PENDING, 30000);
                ::SetEvent(g_StopEvent);
            }
            return NO_ERROR;
        case SERVICE_CONTROL_PARAMCHANGE:
            // Placeholder for future hot-reload of config.ini without a
            // service restart. Currently a no-op.
            Logger::Instance().Info(L"PARAMCHANGE received (no-op in this build).");
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            ::SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
    g_ServiceStatusHandle = ::RegisterServiceCtrlHandlerExW(
        L"AudioWatchdog", ServiceCtrlHandlerEx, nullptr);
    if (!g_ServiceStatusHandle) return;

    g_StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_StopEvent) {
        g_ServiceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        ReportState(SERVICE_STOPPED);
        return;
    }

    ReportState(SERVICE_START_PENDING, 30000);
    RunCore();
    ::CloseHandle(g_StopEvent);
    g_StopEvent = nullptr;
}

} // namespace

void RunAsService() {
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(L"AudioWatchdog"), ServiceMain },
        { nullptr, nullptr },
    };
    if (!::StartServiceCtrlDispatcherW(table)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Running from a console; fall back to foreground.
            Logger::Instance().Error(L"Not started by SCM; run in a console with --foreground, or install first.");
        }
    }
}

void RunForeground() {
    Config cfg = LoadConfig(ConfigFilePath());

    Logger& logger = Logger::Instance();
    SetupRuntime(cfg, logger, true);

    CoreAudioDeviceLister lister;
    ExclusiveModeStore store;
    WatchdogEngine engine(lister, store, cfg);

    g_StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    engine.Start([&](const ScanReport&) { /* engine already logs */ });

    AudioDeviceWatcher watcher;
    watcher.Start([&]() { engine.RequestRescan(); });

    logger.Info(L"Running in foreground. Press Ctrl+C to stop.");

    ::SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
            ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_SHUTDOWN_EVENT) {
            if (g_StopEvent) ::SetEvent(g_StopEvent);
            return TRUE;
        }
        return FALSE;
    }, TRUE);

    ::WaitForSingleObject(g_StopEvent, INFINITE);

    watcher.Stop();
    engine.Stop();
    if (g_StopEvent) {
        ::CloseHandle(g_StopEvent);
        g_StopEvent = nullptr;
    }
    logger.Info(L"Foreground run finished.");
    logger.Flush();
}

} // namespace aw