#include "app/TrayApp.h"

#include "resource.h"

#include "util/StrUtil.h"
#include "util/Paths.h"
#include "util/ComInitializer.h"
#include "config/Config.h"
#include "logging/Logger.h"
#include "audio/CoreAudioDeviceLister.h"
#include "audio/ExclusiveModeStore.h"
#include "audio/AudioDeviceWatcher.h"
#include "core/WatchdogEngine.h"

#include <windows.h>
#include <shellapi.h>
#include <cstdio>

namespace aw {

namespace {

constexpr wchar_t kWindowClass[] = L"AudioWatchdogTrayWnd";
constexpr wchar_t kTrayMutex[] = L"Local\\AudioWatchdog.TrayApp";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Audio Watchdog";
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayMsg = WM_APP + 10;

enum MenuId : UINT_PTR {
    kCmdScanNow = 1,
    kCmdOpenLog = 2,
    kCmdOpenConfig = 3,
    kCmdStartAtLogon = 4,
    kCmdExit = 5,
};

HINSTANCE      g_hInst = nullptr;
HWND           g_hwnd = nullptr;
HICON          g_hIcon = nullptr;
UINT           g_taskbarCreated = 0;
bool           g_iconAdded = false;
bool           g_exitRequested = false;
NOTIFYICONDATAW g_nid{};
WatchdogEngine* g_engine = nullptr;
AudioDeviceWatcher* g_watcher = nullptr;

std::wstring ExecutablePath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n > 0 ? std::wstring(buf) : L"";
}

void SetTooltip(const std::wstring& text) {
    if (!g_iconAdded) return;
    g_nid.uFlags = NIF_TIP;
    wcsncpy_s(g_nid.szTip, _countof(g_nid.szTip), text.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void RemoveTrayIcon() {
    if (g_iconAdded && g_hwnd) {
        ::Shell_NotifyIconW(NIM_DELETE, &g_nid);
    }
    g_iconAdded = false;
}

void AddTrayIcon(bool withBalloon) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = kTrayIconId;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon = g_hIcon;
    wcscpy_s(g_nid.szTip, L"Audio Watchdog");
    if (::Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        g_iconAdded = true;
        g_nid.uVersion = NOTIFYICON_VERSION_4;
        ::Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    } else {
        Logger::Instance().Error(L"Tray icon could not be created (" +
                                 FormatSystemError(::GetLastError()) + L").");
        return;
    }

    if (withBalloon) {
        g_nid.uFlags = NIF_INFO;
        g_nid.dwInfoFlags = NIIF_INFO;
        g_nid.uTimeout = 5000;
        wcscpy_s(g_nid.szInfoTitle, _countof(g_nid.szInfoTitle), L"Audio Watchdog");
        const std::wstring balloon = L"Running in the notification area.\nLogs: " + LogsDir();
        wcscpy_s(g_nid.szInfo, _countof(g_nid.szInfo), balloon.c_str());
        ::Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }
}

bool StartAtLogonEnabled() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buf[MAX_PATH] = {};
    DWORD type = 0;
    DWORD cb = sizeof(buf);
    const LONG r = ::RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    ::RegCloseKey(key);
    return r == ERROR_SUCCESS && buf[0] != L'\0';
}

void SetStartAtLogon(bool enable) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                          nullptr, &key, nullptr) != ERROR_SUCCESS) {
        Logger::Instance().Error(L"Could not open the Run key to change auto-start.");
        return;
    }
    if (enable) {
        const std::wstring cmd = L"\"" + ExecutablePath() + L"\"";
        ::RegSetValueExW(key, kRunValue, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(cmd.c_str()),
                         static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        ::RegDeleteValueW(key, kRunValue);
    }
    ::RegCloseKey(key);
    Logger::Instance().Info(enable ? L"Auto-start at logon enabled."
                                   : L"Auto-start at logon disabled.");
}

void RequestExit() {
    if (g_exitRequested) return;
    g_exitRequested = true;
    RemoveTrayIcon();
    if (g_watcher) g_watcher->Stop();
    if (g_engine) g_engine->Stop();
    g_watcher = nullptr;
    g_engine = nullptr;
    Logger::Instance().Info(L"Audio Watchdog tray resident stopped.");
    Logger::Instance().Flush();
    ::PostMessageW(g_hwnd, WM_NULL, 0, 0);
    ::PostQuitMessage(0);
}

void OnCommand(UINT_PTR id) {
    switch (id) {
        case kCmdScanNow:
            if (g_engine) {
                SetTooltip(L"Audio Watchdog - scanning...");
                g_engine->RequestRescan();
            }
            break;
        case kCmdOpenLog: {
            const std::wstring sel = L"/select,\"" + LogFilePath() + L"\"";
            ::ShellExecuteW(g_hwnd, L"open", L"explorer.exe", sel.c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case kCmdOpenConfig: {
            const std::wstring file = L"\"" + ConfigFilePath() + L"\"";
            ::ShellExecuteW(g_hwnd, L"open", L"notepad.exe", file.c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case kCmdStartAtLogon:
            SetStartAtLogon(!StartAtLogonEnabled());
            break;
        case kCmdExit:
            RequestExit();
            break;
        default:
            break;
    }
}

void ShowMenu() {
    POINT pt{};
    ::GetCursorPos(&pt);
    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdScanNow), L"Scan now");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdOpenLog), L"Open log file");
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdOpenConfig), L"Open config file");
    ::AppendMenuW(menu, MF_STRING | (StartAtLogonEnabled() ? MF_CHECKED : 0),
                  static_cast<UINT_PTR>(kCmdStartAtLogon), L"Start when I log in");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCmdExit), L"Exit");

    ::SetForegroundWindow(g_hwnd);
    const UINT_PTR choice = static_cast<UINT_PTR>(::TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr));
    ::PostMessageW(g_hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    if (choice != 0) OnCommand(choice);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreated) {
        AddTrayIcon(false);
        return 0;
    }
    switch (msg) {
        case kTrayMsg: {
            const UINT ev = static_cast<UINT>(lParam);
            switch (ev) {
                case WM_LBUTTONDOWN:
                case WM_LBUTTONDBLCLK:
                    if (g_engine) g_engine->RequestRescan();
                    break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    ShowMenu();
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_COMMAND:
            OnCommand(LOWORD(wParam));
            return 0;
        case WM_ENDSESSION:
            RequestExit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int RunTray(HINSTANCE hInstance) {
    // Only one resident instance per session.
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kTrayMutex);
    if (mutex == nullptr || ::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) ::CloseHandle(mutex);
        return 0;
    }

    g_hInst = hInstance;

    // The engine/watcher threads initialize their own COM; an MTA on the main
    // thread is fine for the tray plumbing.
    aw::ComInitializer com(COINIT_MULTITHREADED);

    if (!EnsureProgramDataDirs()) {
        Logger::Instance().Error(L"Could not create the ProgramData folders.");
    }

    Config cfg = LoadConfig(ConfigFilePath());
    Logger& logger = Logger::Instance();
    if (cfg.enableLogging) {
        logger.Configure(LogFilePath(), cfg.logLevel, false);
    }
    logger.Info(L"Audio Watchdog tray resident starting (pid " +
                std::to_wstring(::GetCurrentProcessId()) + L").");

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = ::LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kWindowClass;
    ::RegisterClassW(&wc);

    g_taskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");

    // Hidden top-level window (not shown, so it never occupies the taskbar).
    g_hwnd = ::CreateWindowExW(0, kWindowClass, L"Audio Watchdog", WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        logger.Error(L"Could not create the tray window (" +
                     FormatSystemError(::GetLastError()) + L").");
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
        return 1;
    }

    g_hIcon = ::LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    if (!g_hIcon) {
        logger.Error(L"Icon resource not found; falling back to the default icon.");
        g_hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    AddTrayIcon(true);

    CoreAudioDeviceLister lister;
    ExclusiveModeStore store;
    WatchdogEngine engine(lister, store, cfg);
    AudioDeviceWatcher watcher;
    g_engine = &engine;
    g_watcher = &watcher;

    engine.Start([&](const ScanReport& rep) {
        std::wstring tip = FormatW(L"Audio Watchdog - last scan: %d endpoints, %d fixed%s",
                                   rep.endpointsScanned, rep.fixed,
                                   (rep.failed > 0 ? L" (errors)" : L""));
        SetTooltip(tip);
    });
    watcher.Start([&]() {
        if (g_engine) g_engine->RequestRescan();
    });

    if (!StartAtLogonEnabled()) {
        SetStartAtLogon(true);
        logger.Info(L"Registered to start automatically at logon.");
    }

    logger.Info(L"Audio Watchdog is running in the notification area. Log file: " + LogFilePath());

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    watcher.Stop();
    engine.Stop();
    g_watcher = nullptr;
    g_engine = nullptr;

    RemoveTrayIcon();
    logger.Info(L"Audio Watchdog tray resident stopped.");
    logger.Flush();

    ::DestroyIcon(g_hIcon);
    ::DestroyWindow(g_hwnd);
    ::ReleaseMutex(mutex);
    ::CloseHandle(mutex);
    return 0;
}

} // namespace aw