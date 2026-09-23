#include "app/TrayApp.h"

#include "resource.h"
#include "version.h"

#include "util/StrUtil.h"
#include "util/Paths.h"
#include "util/ServiceUtil.h"
#include "util/ComInitializer.h"
#include "config/Config.h"
#include "logging/Logger.h"
#include "audio/AudioFormatStore.h"
#include "audio/CoreAudioDeviceLister.h"
#include "audio/ExclusiveModeStore.h"
#include "audio/AudioDeviceWatcher.h"
#include "core/WatchdogEngine.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>

#include <atomic>
#include <memory>

namespace aw {

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr wchar_t kWindowClass[] = L"AudioWatchdogTrayWnd";
constexpr wchar_t kTrayMutex[] = L"Local\\AudioWatchdog.TrayApp";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Audio Watchdog";
// Required tooltip text - do not change.
constexpr wchar_t kTooltip[] = L"O Audio Watchdog está em execução";

constexpr UINT kTrayIconId = 1;
constexpr UINT kMsgTray = WM_APP + 10;
constexpr UINT kMsgOpDone = WM_APP + 11;     // wParam = Op, lParam = std::wstring* error (or null)
constexpr UINT kMsgShowStatus = WM_APP + 12; // sent by a second instance
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMs = 2000;

enum MenuId : UINT {
    kCmdTitle = 1,
    kCmdToggle = 2,
    kCmdOpen = 3,
    kCmdExit = 4,
};

enum class Op : WPARAM { Pause, Resume, Exit };

// What the tray shows. Mirrors the service state (or the in-process engine).
enum class UiState { Running, Paused, Stopped, Starting, Stopping, Unknown };

const wchar_t* UiStateText(UiState s) {
    switch (s) {
        case UiState::Running: return L"Em execução";
        case UiState::Paused: return L"Pausado";
        case UiState::Stopped: return L"Parado";
        case UiState::Starting: return L"Iniciando…";
        case UiState::Stopping: return L"Parando…";
        case UiState::Unknown: break;
    }
    return L"Desconhecido";
}

// ---------------------------------------------------------------------------
// Backends: the installed Windows service, or an in-process engine when the
// executable runs portable (no service registered).
// ---------------------------------------------------------------------------

class Backend {
public:
    virtual ~Backend() = default;
    virtual UiState Query() = 0;
    virtual bool Pause(std::wstring& err) = 0;
    virtual bool Resume(std::wstring& err) = 0;   // also starts a stopped service
    virtual bool Shutdown(std::wstring& err) = 0; // stops monitoring cleanly
    virtual bool IsService() const = 0;
    // False once the service has been uninstalled underneath us.
    virtual bool StillValid() { return true; }
};

class ServiceBackend final : public Backend {
public:
    UiState Query() override {
        DWORD state = 0;
        if (!QueryServiceStatusExState(&state)) return UiState::Unknown;
        switch (state) {
            case SERVICE_RUNNING: return UiState::Running;
            case SERVICE_PAUSED: return UiState::Paused;
            case SERVICE_STOPPED: return UiState::Stopped;
            case SERVICE_START_PENDING:
            case SERVICE_CONTINUE_PENDING: return UiState::Starting;
            case SERVICE_STOP_PENDING:
            case SERVICE_PAUSE_PENDING: return UiState::Stopping;
            default: return UiState::Unknown;
        }
    }
    bool Pause(std::wstring& err) override { return PauseService(&err); }
    bool Resume(std::wstring& err) override {
        DWORD state = 0;
        if (QueryServiceStatusExState(&state) && state == SERVICE_STOPPED) {
            return StartServiceNow(&err);
        }
        return ContinueService(&err);
    }
    // A clean SCM stop reports exit code 0, so the recovery actions (which
    // only fire on failures) do not restart the service.
    bool Shutdown(std::wstring& err) override { return StopService(&err); }
    bool IsService() const override { return true; }
    bool StillValid() override { return ServiceExists(); }
};

class LocalBackend final : public Backend {
public:
    explicit LocalBackend(const Config& cfg) : engine_(lister_, store_, formats_, cfg) {}
    ~LocalBackend() override {
        watcher_.Stop();
        engine_.Stop();
    }
    bool Start() {
        if (!engine_.Start()) return false;
        watcher_.Start([this](const DeviceChange& c) { engine_.OnDeviceChange(c); });
        return true;
    }
    UiState Query() override {
        switch (engine_.State()) {
            case WatchdogState::Running: return UiState::Running;
            case WatchdogState::Paused: return UiState::Paused;
            case WatchdogState::Stopping: return UiState::Stopping;
            case WatchdogState::Stopped: return UiState::Stopped;
        }
        return UiState::Unknown;
    }
    bool Pause(std::wstring&) override {
        engine_.Pause();
        return true;
    }
    bool Resume(std::wstring&) override {
        engine_.SetConfig(LoadConfig(ConfigFilePath(), false));
        engine_.Resume();
        return true;
    }
    bool Shutdown(std::wstring&) override {
        watcher_.Stop();
        engine_.Stop();
        return true;
    }
    bool IsService() const override { return false; }

private:
    CoreAudioDeviceLister lister_;
    ExclusiveModeStore store_;
    AudioFormatStore formats_;
    WatchdogEngine engine_;
    AudioDeviceWatcher watcher_;
};

// ---------------------------------------------------------------------------
// State (UI thread only, except where noted)
// ---------------------------------------------------------------------------

HINSTANCE g_hInst = nullptr;
HWND g_hwnd = nullptr;
HWND g_statusDlg = nullptr;
HICON g_iconNormal = nullptr;
HICON g_iconPaused = nullptr;
HICON g_iconLarge = nullptr;
UINT g_taskbarCreated = 0;
bool g_iconAdded = false;
bool g_busy = false;          // a service operation is in flight
bool g_exiting = false;
std::atomic<int> g_opsInFlight{0}; // operation threads still using g_backend
UiState g_state = UiState::Unknown;
std::unique_ptr<Backend> g_backend;

HICON LoadAppIcon(int id, int cx, int cy) {
    HICON icon = nullptr;
    if (FAILED(::LoadIconWithScaleDown(g_hInst, MAKEINTRESOURCEW(id), cx, cy, &icon))) {
        icon = static_cast<HICON>(::LoadImageW(g_hInst, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, 0));
    }
    return icon;
}

HICON CurrentIcon() {
    return (g_state == UiState::Running || g_state == UiState::Starting) ? g_iconNormal : g_iconPaused;
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = kMsgTray;
    nid.hIcon = CurrentIcon();
    wcsncpy_s(nid.szTip, kTooltip, _TRUNCATE);
    // Explorer may not be ready yet at logon; retry briefly.
    for (int i = 0; i < 10 && !g_iconAdded; ++i) {
        g_iconAdded = ::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        if (!g_iconAdded) ::Sleep(500);
    }
    if (!g_iconAdded) {
        Logger::Instance().Error(L"Tray icon could not be created (" +
                                 FormatSystemError(::GetLastError()) + L").");
        return;
    }
    nid.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void UpdateTrayIcon() {
    if (!g_iconAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = CurrentIcon();
    wcsncpy_s(nid.szTip, kTooltip, _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void RemoveTrayIcon() {
    if (!g_iconAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = kTrayIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    g_iconAdded = false;
}

bool CanToggle() {
    return !g_busy && (g_state == UiState::Running || g_state == UiState::Paused ||
                       g_state == UiState::Stopped);
}

const wchar_t* ToggleText() {
    return g_state == UiState::Running ? L"Pausar monitoramento" : L"Retomar monitoramento";
}

// ---------------------------------------------------------------------------
// Status window ("Abrir")
// ---------------------------------------------------------------------------

void RefreshStatusDialog() {
    if (!g_statusDlg) return;
    const Config cfg = LoadConfig(ConfigFilePath(), false);
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_TITLE, L"Audio Watchdog " AWW_VERSION_WSTR);
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_STATE,
                      (std::wstring(L"Status: ") + UiStateText(g_state)).c_str());
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_EXCL,
                      cfg.exclusiveModeProtection
                          ? L"Proteção contra modo exclusivo: ATIVADA"
                          : L"Proteção contra modo exclusivo: DESATIVADA (config.ini)");
    const std::wstring fmt = cfg.formatStandardization
        ? L"Padronização de formato: ATIVADA (" + DescribeFormatTarget(cfg) + L")"
        : std::wstring(L"Padronização de formato: DESATIVADA");
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_FORMAT, fmt.c_str());
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_MODE,
                      g_backend && g_backend->IsService()
                          ? L"Modo: serviço do Windows (AudioWatchdog)"
                          : L"Modo: portátil (sem serviço instalado)");
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_LOGS, (L"Logs: " + LogsDir()).c_str());
    ::SetDlgItemTextW(g_statusDlg, IDC_STATUS_TOGGLE, ToggleText());
    ::EnableWindow(::GetDlgItem(g_statusDlg, IDC_STATUS_TOGGLE), CanToggle());
}

void RunOp(Op op);

INT_PTR CALLBACK StatusDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            g_statusDlg = dlg;
            const int size = ::MulDiv(48, static_cast<int>(::GetDpiForWindow(dlg)), 96);
            if (g_iconLarge) ::DestroyIcon(g_iconLarge);
            g_iconLarge = LoadAppIcon(IDI_APP, size, size);
            ::SendDlgItemMessageW(dlg, IDC_STATUS_ICON, STM_SETICON,
                                  reinterpret_cast<WPARAM>(g_iconLarge), 0);
            ::SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_iconLarge));
            ::SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_iconNormal));
            HFONT font = reinterpret_cast<HFONT>(::SendMessageW(dlg, WM_GETFONT, 0, 0));
            LOGFONTW lf{};
            if (font && ::GetObjectW(font, sizeof(lf), &lf)) {
                lf.lfWeight = FW_SEMIBOLD;
                lf.lfHeight = lf.lfHeight * 4 / 3;
                HFONT bold = ::CreateFontIndirectW(&lf);
                ::SetPropW(dlg, L"AW.TitleFont", bold);
                ::SendDlgItemMessageW(dlg, IDC_STATUS_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(bold), TRUE);
            }
            RefreshStatusDialog();
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_STATUS_TOGGLE:
                    RunOp(g_state == UiState::Running ? Op::Pause : Op::Resume);
                    return TRUE;
                case IDC_STATUS_OPENLOGS:
                    EnsureLogsDir();
                    ::ShellExecuteW(dlg, L"open", LogsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return TRUE;
                case IDOK:
                case IDCANCEL:
                    ::DestroyWindow(dlg);
                    return TRUE;
                default:
                    break;
            }
            break;
        case WM_DESTROY:
            if (HANDLE bold = ::RemovePropW(dlg, L"AW.TitleFont")) ::DeleteObject(bold);
            g_statusDlg = nullptr;
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

void ShowStatusWindow() {
    if (g_statusDlg) {
        ::ShowWindow(g_statusDlg, SW_RESTORE);
        ::SetForegroundWindow(g_statusDlg);
        return;
    }
    HWND dlg = ::CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_STATUS), nullptr, StatusDlgProc, 0);
    if (dlg) {
        ::ShowWindow(dlg, SW_SHOWNORMAL);
        ::SetForegroundWindow(dlg);
    }
}

// ---------------------------------------------------------------------------
// State refresh and operations
// ---------------------------------------------------------------------------

void QuitTray() {
    if (g_exiting) return;
    g_exiting = true;
    ::KillTimer(g_hwnd, kRefreshTimer);
    if (g_statusDlg) ::DestroyWindow(g_statusDlg);
    RemoveTrayIcon();
    ::DestroyWindow(g_hwnd);
}

void RefreshState() {
    if (!g_backend || g_exiting) return;
    if (!g_backend->StillValid()) {
        // The service was uninstalled: nothing left to control.
        Logger::Instance().Info(L"Service no longer installed; closing the tray icon.");
        QuitTray();
        return;
    }
    const UiState now = g_backend->Query();
    if (now != g_state) {
        g_state = now;
        UpdateTrayIcon();
    }
    RefreshStatusDialog();
}

struct OpContext {
    Op op;
    Backend* backend;
    HWND hwnd;
};

DWORD WINAPI OpThread(LPVOID arg) {
    std::unique_ptr<OpContext> ctx(static_cast<OpContext*>(arg));
    std::wstring err;
    bool ok = false;
    switch (ctx->op) {
        case Op::Pause: ok = ctx->backend->Pause(err); break;
        case Op::Resume: ok = ctx->backend->Resume(err); break;
        case Op::Exit: ok = ctx->backend->Shutdown(err); break;
    }
    auto* error = ok ? nullptr : new std::wstring(err.empty() ? L"erro desconhecido" : err);
    if (!::PostMessageW(ctx->hwnd, kMsgOpDone, static_cast<WPARAM>(ctx->op), reinterpret_cast<LPARAM>(error))) {
        delete error;
    }
    --g_opsInFlight;
    return 0;
}

// Service calls may take a few seconds (they wait for the state change), so
// they run off the UI thread; the result comes back as kMsgOpDone.
void RunOp(Op op) {
    if (g_busy || !g_backend) return;
    g_busy = true;
    RefreshStatusDialog();
    auto* ctx = new OpContext{ op, g_backend.get(), g_hwnd };
    ++g_opsInFlight;
    HANDLE t = ::CreateThread(nullptr, 0, OpThread, ctx, 0, nullptr);
    if (t) {
        ::CloseHandle(t);
    } else {
        --g_opsInFlight;
        delete ctx;
        g_busy = false;
    }
}

void OnOpDone(Op op, std::wstring* error) {
    std::unique_ptr<std::wstring> err(error);
    g_busy = false;
    Logger& log = Logger::Instance();
    const wchar_t* what = op == Op::Pause ? L"pause" : op == Op::Resume ? L"resume" : L"stop";
    if (err) {
        log.Error(FormatW(L"Tray: %s failed: %s", what, err->c_str()));
    } else {
        log.Info(FormatW(L"Tray: %s requested by the user.", what));
    }

    if (op == Op::Exit) {
        if (err) {
            const std::wstring msg =
                L"Não foi possível parar o serviço Audio Watchdog:\n" + *err +
                L"\n\nO ícone será fechado, mas a proteção continua ativa em segundo plano.";
            ::MessageBoxW(nullptr, msg.c_str(), L"Audio Watchdog", MB_OK | MB_ICONWARNING);
        }
        QuitTray();
        return;
    }
    if (err) {
        const std::wstring msg = std::wstring(op == Op::Pause ? L"Não foi possível pausar"
                                                               : L"Não foi possível retomar") +
                                 L" o monitoramento:\n" + *err;
        ::MessageBoxW(g_statusDlg, msg.c_str(), L"Audio Watchdog", MB_OK | MB_ICONWARNING);
    }
    RefreshState();
}

void ConfirmExit() {
    const wchar_t* text = g_backend && g_backend->IsService()
        ? L"Encerrar o Audio Watchdog?\n\n"
          L"O serviço de proteção será parado e o ícone removido. A proteção volta "
          L"automaticamente na próxima inicialização do Windows ou quando você abrir o "
          L"Audio Watchdog novamente."
        : L"Encerrar o Audio Watchdog?\n\nO monitoramento dos dispositivos de áudio será interrompido.";
    ::SetForegroundWindow(g_hwnd);
    if (::MessageBoxW(nullptr, text, L"Audio Watchdog",
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND) == IDYES) {
        RunOp(Op::Exit);
    }
}

void ShowMenu(POINT pt) {
    RefreshState();
    if (g_exiting) return;
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;
    ::AppendMenuW(menu, MF_STRING, kCmdTitle, L"Audio Watchdog");
    ::SetMenuDefaultItem(menu, kCmdTitle, FALSE);
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring status = std::wstring(L"Status: ") + UiStateText(g_state);
    ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (CanToggle() ? 0 : MF_GRAYED), kCmdToggle, ToggleText());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdOpen, L"Abrir");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (g_busy ? MF_GRAYED : 0), kCmdExit, L"Encerrar");

    // Required so the menu closes when the user clicks elsewhere.
    ::SetForegroundWindow(g_hwnd);
    const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON |
                       (::GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    const UINT choice = static_cast<UINT>(::TrackPopupMenuEx(menu, flags, pt.x, pt.y, g_hwnd, nullptr));
    ::PostMessageW(g_hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    switch (choice) {
        case kCmdTitle:
        case kCmdOpen:
            ShowStatusWindow();
            break;
        case kCmdToggle:
            RunOp(g_state == UiState::Running ? Op::Pause : Op::Resume);
            break;
        case kCmdExit:
            ConfirmExit();
            break;
        default:
            break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreated && g_taskbarCreated != 0) {
        // Explorer restarted: the icon has to be added again.
        g_iconAdded = false;
        AddTrayIcon();
        return 0;
    }
    switch (msg) {
        case kMsgTray:
            // NOTIFYICON_VERSION_4: the event is in LOWORD(lParam) and the
            // anchor point in wParam.
            switch (LOWORD(lParam)) {
                case WM_CONTEXTMENU:
                    ShowMenu(POINT{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
                    break;
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    ShowStatusWindow();
                    break;
                default:
                    break;
            }
            return 0;
        case kMsgOpDone:
            OnOpDone(static_cast<Op>(wParam), reinterpret_cast<std::wstring*>(lParam));
            return 0;
        case kMsgShowStatus:
            ShowStatusWindow();
            return 0;
        case WM_TIMER:
            if (wParam == kRefreshTimer) RefreshState();
            return 0;
        case WM_CLOSE:
            // Sent by setup/uninstall: close the icon only; the service
            // lifecycle is handled by the installer itself.
            QuitTray();
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) QuitTray();
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// v1.0 registered a per-user Run value; installed copies now use a machine-wide
// one. Remove the old value so the app does not start twice.
void CleanupLegacyAutostart() {
    HKEY hklm = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRunKey, 0, KEY_QUERY_VALUE, &hklm) != ERROR_SUCCESS) return;
    const bool machineWide = ::RegQueryValueExW(hklm, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    ::RegCloseKey(hklm);
    if (!machineWide) return;
    HKEY hkcu = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hkcu) == ERROR_SUCCESS) {
        ::RegDeleteValueW(hkcu, kRunValue);
        ::RegCloseKey(hkcu);
    }
}

} // namespace

int RunTray(HINSTANCE hInstance) {
    // Only one tray per session; a second launch opens the status window.
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kTrayMutex);
    if (mutex == nullptr || ::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = ::FindWindowW(kWindowClass, nullptr)) {
            DWORD pid = 0;
            ::GetWindowThreadProcessId(existing, &pid);
            ::AllowSetForegroundWindow(pid);
            ::PostMessageW(existing, kMsgShowStatus, 0, 0);
        }
        if (mutex) ::CloseHandle(mutex);
        return 0;
    }

    g_hInst = hInstance;
    aw::ComInitializer com(COINIT_APARTMENTTHREADED);

    const bool serviceMode = ServiceExists();
    const Config cfg = LoadConfig(ConfigFilePath(), !serviceMode);
    Logger& logger = Logger::Instance();
    EnsureLogsDir();
    if (cfg.enableLogging) {
        // The service owns audiowatchdog.log; the per-user tray writes its own.
        logger.Configure(serviceMode ? TrayLogFilePath() : LogFilePath(), cfg.logLevel, false);
    }
    logger.Info(L"Audio Watchdog " AWW_VERSION_WSTR L" tray starting (pid " +
                std::to_wstring(::GetCurrentProcessId()) + L", " +
                (serviceMode ? L"service mode" : L"portable mode") + L").");

    if (serviceMode) {
        g_backend = std::make_unique<ServiceBackend>();
        // Opening the app brings the protection back if it was stopped.
        std::wstring err;
        if (g_backend->Query() == UiState::Stopped && !g_backend->Resume(err)) {
            logger.Warn(L"Could not start the Audio Watchdog service: " + err);
        }
        CleanupLegacyAutostart();
    } else {
        auto local = std::make_unique<LocalBackend>(cfg);
        if (!local->Start()) {
            logger.Error(L"Could not start the watchdog engine.");
            ::MessageBoxW(nullptr, L"Não foi possível iniciar o monitoramento de áudio.",
                          L"Audio Watchdog", MB_OK | MB_ICONERROR);
            ::ReleaseMutex(mutex);
            ::CloseHandle(mutex);
            return 1;
        }
        g_backend = std::move(local);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = ::LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);

    g_taskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");
    // Allow Explorer (medium IL) to deliver TaskbarCreated even if we run elevated.
    ::ChangeWindowMessageFilter(g_taskbarCreated, MSGFLT_ADD);

    // Hidden top-level window (never shown) that owns the tray icon.
    g_hwnd = ::CreateWindowExW(0, kWindowClass, L"Audio Watchdog", WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        logger.Error(L"Could not create the tray window (" + FormatSystemError(::GetLastError()) + L").");
        g_backend.reset();
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
        return 1;
    }

    const int cx = ::GetSystemMetrics(SM_CXSMICON);
    const int cy = ::GetSystemMetrics(SM_CYSMICON);
    g_iconNormal = LoadAppIcon(IDI_APP, cx, cy);
    g_iconPaused = LoadAppIcon(IDI_APP_PAUSED, cx, cy);
    if (!g_iconNormal) g_iconNormal = ::LoadIconW(nullptr, IDI_APPLICATION);
    if (!g_iconPaused) g_iconPaused = g_iconNormal;

    g_state = g_backend->Query();
    AddTrayIcon();
    ::SetTimer(g_hwnd, kRefreshTimer, kRefreshMs, nullptr);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_statusDlg && ::IsDialogMessageW(g_statusDlg, &msg)) continue;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // An operation thread may still be running against the backend; wait
    // for it (its SCM waits are bounded) before tearing the backend down.
    while (g_opsInFlight.load() > 0) ::Sleep(50);

    g_backend.reset();
    logger.Info(L"Audio Watchdog tray stopped.");
    logger.Flush();

    if (g_iconPaused && g_iconPaused != g_iconNormal) ::DestroyIcon(g_iconPaused);
    if (g_iconNormal) ::DestroyIcon(g_iconNormal);
    if (g_iconLarge) ::DestroyIcon(g_iconLarge);
    ::ReleaseMutex(mutex);
    ::CloseHandle(mutex);
    return 0;
}

} // namespace aw
