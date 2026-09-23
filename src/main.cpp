#include "app/TrayApp.h"
#include "service/AudioWatchdogService.h"
#include "cli/Cli.h"
#include "util/StrUtil.h"

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <iostream>

namespace {

// GUI-subsystem executable: when launched from a console the CRT already
// inherits the console handles, but when double-clicked there is no console,
// so (re)attach to the parent or allocate one for CLI output.
void EnsureConsoleOutput() {
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE) {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
            ::AllocConsole();
        }
        FILE* f = nullptr;
        _wfreopen_s(&f, L"CONOUT$", L"w", stdout);
        _wfreopen_s(&f, L"CONOUT$", L"w", stderr);
    }
    std::wcout.clear();
    std::wcerr.clear();
}

} // namespace

// Entry point. The default (no arguments) launches the resident tray app;
// explicit commands (install/scan/devices/...) run one-shot CLI tools.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrev*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) {
        // Should never happen; fall back to running in the background.
        return aw::RunTray(hInstance);
    }

    if (argc < 2) {
        ::LocalFree(argv);
        return aw::RunTray(hInstance);
    }

    const std::wstring arg1 = aw::ToLowerA(argv[1]);
    if (arg1 == L"--service") {
        ::LocalFree(argv);
        aw::RunAsService();
        return 0;
    }
    if (arg1 == L"--foreground" || arg1 == L"foreground") {
        EnsureConsoleOutput();
        ::LocalFree(argv);
        aw::RunForeground();
        return 0;
    }

    EnsureConsoleOutput();
    const int rc = aw::RunCli(argc, argv);
    ::LocalFree(argv);
    return rc;
}