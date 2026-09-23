#include "cli/Cli.h"

#include "util/StrUtil.h"
#include "util/ServiceUtil.h"
#include "util/Paths.h"
#include "util/ComInitializer.h"
#include "config/Config.h"
#include "logging/Logger.h"
#include "core/WatchdogEngine.h"
#include "audio/CoreAudioDeviceLister.h"
#include "audio/ExclusiveModeStore.h"
#include "cli/Diagnostic.h"

#include <windows.h>
#include <shellapi.h>
#include <iostream>

namespace aw {

namespace {

constexpr wchar_t kVersion[] = L"1.0.0";

std::wstring ExecutablePath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n > 0 ? std::wstring(buf) : L"";
}

bool IsElevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te{};
        DWORD cb = 0;
        if (::GetTokenInformation(token, TokenElevation, &te, sizeof(te), &cb)) elevated = te.TokenIsElevated;
        ::CloseHandle(token);
    }
    return elevated != FALSE;
}

// Re-launches the process elevated and exits. Returns true if relaunched.
bool RelaunchElevated() {
    const std::wstring exe = ExecutablePath();
    const std::wstring cmd = ::GetCommandLineW();
    HINSTANCE r = ::ShellExecuteW(nullptr, L"runas", exe.c_str(),
                                  cmd.substr(cmd.find(exe) + exe.size()).c_str(),
                                  nullptr, SW_HIDE);
    if (reinterpret_cast<INT_PTR>(r) > 32) {
        std::wcout << L"Administrator approval requested in a separate window.\n";
        return true;
    }
    return false;
}

bool RequireAdmin(const wchar_t* what) {
    if (IsElevated()) return true;
    std::wcout << what << L" requires administrator rights.\n";
    if (RelaunchElevated()) return false;
    std::wcout << L"Elevation failed; please run from an elevated prompt.\n";
    return false;
}

int CmdVersion() {
    std::wcout << L"Audio Watchdog version " << kVersion << L"\n";
    return 0;
}

int CmdInstall() {
    if (!RequireAdmin(L"install")) return 1;
    std::wstring binary = L"\"" + ExecutablePath() + L"\" --service";
    std::wstring err;
    if (!InstallService(binary, &err)) {
        std::wcout << L"Install failed: " << err << L"\n";
        return 1;
    }
    std::wcout << L"Installed. Use: AudioWatchdog start\n";
    return 0;
}

int CmdUninstall() {
    if (!RequireAdmin(L"uninstall")) return 1;
    std::wstring err;
    if (!UninstallService(&err)) {
        std::wcout << L"Uninstall failed: " << err << L"\n";
        return 1;
    }
    std::wcout << L"Uninstalled.\n";
    return 0;
}

int CmdStart() {
    if (!RequireAdmin(L"start")) return 1;
    std::wstring err;
    if (!StartServiceNow(&err)) {
        std::wcout << L"Start failed: " << err << L"\n";
        return 1;
    }
    std::wcout << L"Started.\n";
    return 0;
}

int CmdStop() {
    if (!RequireAdmin(L"stop")) return 1;
    std::wstring err;
    if (!StopService(&err)) {
        std::wcout << L"Stop failed: " << err << L"\n";
        return 1;
    }
    std::wcout << L"Stopped.\n";
    return 0;
}

int CmdRestart() {
    if (!RequireAdmin(L"restart")) return 1;
    std::wstring err;
    if (!RestartService(&err)) {
        std::wcout << L"Restart failed: " << err << L"\n";
        return 1;
    }
    std::wcout << L"Restarted.\n";
    return 0;
}

int CmdStatus() {
    std::wstring err;
    if (!aw::PrintServiceStatus(&err)) return 1;
    return 0;
}

int CmdScan() {
    aw::ComInitializer com(COINIT_MULTITHREADED);
    if (!com.ok()) {
        std::wcout << L"COM init failed\n";
        return 2;
    }
    Config cfg = LoadConfig(ConfigFilePath());
    CoreAudioDeviceLister lister;
    ExclusiveModeStore store;
    WatchdogEngine engine(lister, store, cfg);
    ScanReport rep = engine.ScanOnce();
    std::wcout << L"Scan complete: " << rep.endpointsScanned << L" endpoint(s) | already off: "
               << rep.alreadyOff << L" | fixed: " << rep.fixed << L" | failed: " << rep.failed
               << L" | skipped: " << rep.skipped << L"\n";
    return rep.failed > 0 ? 1 : 0;
}

int CmdDevices() {
    return RunDevicesCommand();
}

int CmdDiagnose(int argc, wchar_t** argv) {
    bool probe = false;
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = ToLowerA(argv[i]);
        if (a == L"--probe-exclusive" || a == L"--probe") probe = true;
    }
    return RunDiagnoseCommand(probe);
}

} // namespace

void PrintUsage() {
    std::wcout << L"Audio Watchdog - prevents exclusive audio mode\n\n"
               << L"Usage: AudioWatchdog <command> [options]\n\n"
               << L"(no arguments)    Run in the system-tray background (default)\n\n"
               << L"Commands:\n"
               << L"  install            Install the Windows service\n"
               << L"  uninstall          Remove the Windows service\n"
               << L"  start              Start the service\n"
               << L"  stop               Stop the service\n"
               << L"  restart            Restart the service\n"
               << L"  status             Show the service status\n"
               << L"  scan               Run one enforcement pass (connection-safe)\n"
               << L"  devices            List audio endpoints and their exclusive state\n"
               << L"  diagnose           Full self-check of the environment\n"
               << L"                      --probe-exclusive  also opens a real WASAPI stream\n"
               << L"  version            Show version\n"
               << L"  help               Show this help\n\n"
               << L"Options:\n"
               << L"  --foreground       Run the watchdog loop as a console app (debug)\n"
               << L"  --service          Internal: run as a Windows service\n";
}

int RunCli(int argc, wchar_t** argv) {
    if (argc < 2) {
        PrintUsage();
        return 0;
    }

    const std::wstring cmd = ToLowerA(argv[1]);
    if (cmd == L"install") return CmdInstall();
    if (cmd == L"uninstall") return CmdUninstall();
    if (cmd == L"start") return CmdStart();
    if (cmd == L"stop") return CmdStop();
    if (cmd == L"restart") return CmdRestart();
    if (cmd == L"status") return CmdStatus();
    if (cmd == L"scan") return CmdScan();
    if (cmd == L"devices") return CmdDevices();
    if (cmd == L"diagnose") return CmdDiagnose(argc, argv);
    if (cmd == L"version") return CmdVersion();
    if (cmd == L"help" || cmd == L"-h" || cmd == L"--help" || cmd == L"/?") {
        PrintUsage();
        return 0;
    }

    std::wcout << L"Unknown command: " << argv[1] << L"\n";
    PrintUsage();
    return 1;
}

} // namespace aw