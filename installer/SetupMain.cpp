// Audio Watchdog Setup / Uninstall
//
// Self-contained Win32 installer: embeds AudioWatchdog.exe as an RCDATA
// resource and
//   - installs it (default: Program Files\Audio Watchdog) with a logs\ folder,
//   - writes the configuration chosen in the wizard (ProgramData),
//   - installs/updates and starts the "AudioWatchdog" Windows service,
//   - registers the tray app for every user at logon, a Start Menu folder,
//     an Apps & Features entry and Uninstall.exe (a copy of this program).
//
// Command line
//   (none)                  interactive wizard
//   /S                      silent install (keeps an existing configuration)
//   /format=48000:24        silent: enable format standardization (rate:bits)
//   /noformat               silent: disable format standardization
//   /dir="C:\path"          install folder
//   /notray                 do not launch the tray app at the end
//   /uninstall [/S] [/removedata]
// Uninstall.exe runs the uninstaller without arguments.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <exdisp.h>
#include <commctrl.h>

#include "setup_res.h"
#include "version.h"

#include "config/Config.h"
#include "logging/Logger.h"
#include "util/Paths.h"
#include "util/ServiceUtil.h"
#include "util/StrUtil.h"

#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace aw;

namespace {

constexpr wchar_t kProduct[] = L"Audio Watchdog";
constexpr wchar_t kExeName[] = L"AudioWatchdog.exe";
constexpr wchar_t kUninstallerName[] = L"Uninstall.exe";
constexpr wchar_t kTrayWindowClass[] = L"AudioWatchdogTrayWnd";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AudioWatchdog";
constexpr wchar_t kEventLogKey[] = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\Audio Watchdog";
constexpr wchar_t kTitle[] = L"Audio Watchdog";

// ---------------------------------------------------------------------------
// Options / small helpers
// ---------------------------------------------------------------------------

struct Options {
    bool silent = false;
    bool uninstall = false;
    bool uninstallStage2 = false;
    bool removeData = false;
    bool launchTray = true;
    bool formatSet = false;      // /format or /noformat given
    bool format = false;
    std::uint32_t sampleRate = kDefaultSampleRate;
    std::uint16_t bitDepth = kDefaultBitDepth;
    std::wstring dir;
};

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, 0, nullptr, &path))) out = path;
    ::CoTaskMemFree(path);
    return out;
}

std::wstring StartMenuDir() { return KnownFolder(FOLDERID_CommonPrograms) + L"\\" + kProduct; }

bool FileExists(const std::wstring& p) { return ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool CreateDirTree(const std::wstring& dir) {
    const int r = ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

bool DeleteTree(const std::wstring& dir) {
    if (!FileExists(dir)) return true;
    std::wstring from = dir;
    from.push_back(L'\0'); // double-null terminated
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    return ::SHFileOperationW(&op) == 0;
}

// Deletes a file, or schedules it for deletion at reboot when it is in use.
void DeleteOrSchedule(const std::wstring& path) {
    if (!FileExists(path)) return;
    if (!::DeleteFileW(path.c_str())) ::MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

std::wstring RegReadString(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[1024] = {};
    DWORD cb = sizeof(buf);
    if (::RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS) return buf;
    return L"";
}

void RegSetString(HKEY key, const wchar_t* name, const std::wstring& value, DWORD type = REG_SZ) {
    ::RegSetValueExW(key, name, 0, type, reinterpret_cast<const BYTE*>(value.c_str()),
                     static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void RegSetDword(HKEY key, const wchar_t* name, DWORD value) {
    ::RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

std::wstring DefaultInstallDir() {
    const std::wstring existing = RegReadString(HKEY_LOCAL_MACHINE, kUninstallKey, L"InstallLocation");
    if (!existing.empty()) return existing;
    return KnownFolder(FOLDERID_ProgramFiles) + L"\\" + kProduct;
}

void SetupLog(const std::wstring& dir, const std::wstring& line) {
    const std::wstring path = dir + L"\\logs\\setup.log";
    HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    const std::string utf8 = Utf8FromWide(FormatW(L"[%04u-%02u-%02u %02u:%02u:%02u] ", st.wYear, st.wMonth,
                                                  st.wDay, st.wHour, st.wMinute, st.wSecond) + line + L"\r\n");
    DWORD written = 0;
    ::WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    ::CloseHandle(h);
}

// Closes the tray icon of the current session (WM_CLOSE only closes the
// icon, it does not stop the service).
void CloseTrayWindows() {
    for (int i = 0; i < 30; ++i) {
        HWND w = ::FindWindowW(kTrayWindowClass, nullptr);
        if (!w) return;
        ::PostMessageW(w, WM_CLOSE, 0, 0);
        ::Sleep(100);
    }
}

bool CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args,
                    const std::wstring& description) {
    IShellLinkW* link = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        return false;
    }
    bool ok = false;
    link->SetPath(target.c_str());
    link->SetArguments(args.c_str());
    link->SetWorkingDirectory(target.substr(0, target.find_last_of(L'\\')).c_str());
    link->SetDescription(description.c_str());
    link->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    return ok;
}

// Launches a program as the logged-on user (not elevated) by asking the
// desktop shell to start it.
bool LaunchUnelevated(const std::wstring& exe) {
    bool ok = false;
    IShellWindows* windows = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) {
        VARIANT loc{};
        VARIANT empty{};
        long hwnd = 0;
        IDispatch* disp = nullptr;
        if (windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp) == S_OK && disp) {
            IServiceProvider* sp = nullptr;
            if (SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&sp)))) {
                IShellBrowser* browser = nullptr;
                if (SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) {
                    IShellView* view = nullptr;
                    if (SUCCEEDED(browser->QueryActiveShellView(&view))) {
                        IDispatch* bg = nullptr;
                        if (SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&bg)))) {
                            IShellFolderViewDual* folderView = nullptr;
                            if (SUCCEEDED(bg->QueryInterface(IID_PPV_ARGS(&folderView)))) {
                                IDispatch* app = nullptr;
                                if (SUCCEEDED(folderView->get_Application(&app))) {
                                    IShellDispatch2* shell = nullptr;
                                    if (SUCCEEDED(app->QueryInterface(IID_PPV_ARGS(&shell)))) {
                                        BSTR file = ::SysAllocString(exe.c_str());
                                        VARIANT none{};
                                        ok = SUCCEEDED(shell->ShellExecute(file, none, none, none, none));
                                        ::SysFreeString(file);
                                        shell->Release();
                                    }
                                    app->Release();
                                }
                                folderView->Release();
                            }
                            bg->Release();
                        }
                        view->Release();
                    }
                    browser->Release();
                }
                sp->Release();
            }
            disp->Release();
        }
        windows->Release();
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

using Progress = std::function<void(int percent, const std::wstring& text)>;

bool WritePayload(const std::wstring& exePath, std::wstring& error) {
    HMODULE self = ::GetModuleHandleW(nullptr);
    HRSRC res = ::FindResourceW(self, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
    HGLOBAL hg = res ? ::LoadResource(self, res) : nullptr;
    const void* data = hg ? ::LockResource(hg) : nullptr;
    const DWORD size = res ? ::SizeofResource(self, res) : 0;
    if (!data || size == 0) {
        error = L"O instalador está corrompido (payload ausente).";
        return false;
    }

    // A running copy (e.g. a tray in another user session) keeps the file
    // locked; renaming a running executable is allowed, so move it aside.
    if (FileExists(exePath) && !::DeleteFileW(exePath.c_str())) {
        const std::wstring old = exePath + L".old";
        ::DeleteFileW(old.c_str());
        if (::MoveFileExW(exePath.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ::MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    }

    const std::wstring tmp = exePath + L".new";
    HANDLE h = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = L"Não foi possível gravar " + exePath + L": " + FormatSystemError(::GetLastError());
        return false;
    }
    DWORD written = 0;
    const BOOL ok = ::WriteFile(h, data, size, &written, nullptr);
    ::CloseHandle(h);
    if (!ok || written != size ||
        !::MoveFileExW(tmp.c_str(), exePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = L"Não foi possível gravar " + exePath + L": " + FormatSystemError(::GetLastError());
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

void RegisterEventSource() {
    // .NET's generic message file (present on every Windows 10/11) renders
    // the plain-text events written by the service.
    const std::wstring dll = L"%SystemRoot%\\Microsoft.NET\\Framework64\\v4.0.30319\\EventLogMessages.dll";
    wchar_t expanded[MAX_PATH] = {};
    ::ExpandEnvironmentStringsW(dll.c_str(), expanded, MAX_PATH);
    if (!FileExists(expanded)) return;
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, kEventLogKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                          nullptr) == ERROR_SUCCESS) {
        RegSetString(key, L"EventMessageFile", dll, REG_EXPAND_SZ);
        RegSetDword(key, L"TypesSupported", 7);
        ::RegCloseKey(key);
    }
}

void RegisterUninstallEntry(const std::wstring& dir) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                          nullptr) != ERROR_SUCCESS) {
        return;
    }
    const std::wstring exe = dir + L"\\" + kExeName;
    const std::wstring uninstaller = L"\"" + dir + L"\\" + kUninstallerName + L"\"";
    RegSetString(key, L"DisplayName", kProduct);
    RegSetString(key, L"DisplayVersion", AWW_VERSION_WSTR);
    RegSetString(key, L"Publisher", L"caio-kenai");
    RegSetString(key, L"DisplayIcon", exe + L",0");
    RegSetString(key, L"InstallLocation", dir);
    RegSetString(key, L"UninstallString", uninstaller);
    RegSetString(key, L"QuietUninstallString", uninstaller + L" /S");
    RegSetString(key, L"URLInfoAbout", L"https://github.com/caio-kenai/Audio-Watchdog");
    RegSetDword(key, L"NoModify", 1);
    RegSetDword(key, L"NoRepair", 1);
    RegSetDword(key, L"VersionMajor", AWW_VERSION_MAJOR);
    RegSetDword(key, L"VersionMinor", AWW_VERSION_MINOR);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    DWORD kb = 1024;
    if (::GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fad)) kb = fad.nFileSizeLow / 1024 * 2;
    RegSetDword(key, L"EstimatedSize", kb);
    ::RegCloseKey(key);
}

bool Install(const Options& opt, const Progress& progress, std::wstring& error) {
    const std::wstring dir = opt.dir;
    const std::wstring exe = dir + L"\\" + kExeName;
    const std::wstring logs = dir + L"\\logs";

    progress(5, L"Preparando...");
    if (!CreateDirTree(dir)) {
        error = L"Não foi possível criar a pasta de instalação: " + FormatSystemError(::GetLastError());
        return false;
    }
    CreateDirTree(logs);
    ApplyLogsDirAcl(logs);
    SetupLog(dir, L"Installing Audio Watchdog " AWW_VERSION_WSTR L" into " + dir);

    // 1. Stop what is running (the service releases the executable).
    progress(15, L"Parando a versão em execução...");
    CloseTrayWindows();
    if (ServiceExists()) {
        std::wstring err;
        if (!StopService(&err)) SetupLog(dir, L"Could not stop the running service: " + err);
    }

    // 2. Binaries.
    progress(30, L"Copiando arquivos...");
    if (!WritePayload(exe, error)) return false;
    wchar_t self[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring uninstaller = dir + L"\\" + kUninstallerName;
    if (_wcsicmp(self, uninstaller.c_str()) != 0 && !::CopyFileW(self, uninstaller.c_str(), FALSE)) {
        SetupLog(dir, L"Could not write Uninstall.exe: " + FormatSystemError(::GetLastError()));
    }

    // 3. Configuration (keeps every existing setting, applies the choices).
    progress(45, L"Gravando configuração...");
    const std::wstring dataDir = ProgramDataDir();
    CreateDirTree(dataDir);
    ApplyConfigDirAcl(dataDir);
    Config cfg = LoadConfig(ConfigFilePath(), false);
    cfg.exclusiveModeProtection = true; // mandatory feature
    if (opt.formatSet) {
        cfg.formatStandardization = opt.format;
        cfg.sampleRate = opt.sampleRate;
        cfg.bitDepth = opt.bitDepth;
    }
    if (!SaveConfig(cfg, ConfigFilePath())) {
        error = L"Não foi possível gravar " + ConfigFilePath() + L": " + FormatSystemError(::GetLastError());
        return false;
    }
    SetupLog(dir, std::wstring(L"Exclusive Mode Protection: ENABLED | Format Standardization: ") +
                      (cfg.formatStandardization ? L"ENABLED (" + DescribeFormatTarget(cfg) + L")" : L"DISABLED"));

    // 4. Windows service (automatic start, recovery, permissions).
    progress(60, L"Instalando o serviço do Windows...");
    std::wstring err;
    if (!InstallService(L"\"" + exe + L"\" --service", &err)) {
        error = L"Não foi possível instalar o serviço: " + err;
        return false;
    }
    RegisterEventSource();

    // 5. Tray app for every user at logon, Start Menu, Apps & Features.
    progress(75, L"Registrando atalhos...");
    HKEY run = nullptr;
    if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &run, nullptr) ==
        ERROR_SUCCESS) {
        RegSetString(run, kProduct, L"\"" + exe + L"\"");
        ::RegCloseKey(run);
    }
    HKEY userRun = nullptr; // v1.0 used a per-user entry
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &userRun) == ERROR_SUCCESS) {
        ::RegDeleteValueW(userRun, kProduct);
        ::RegCloseKey(userRun);
    }
    ::DeleteFileW((KnownFolder(FOLDERID_Programs) + L"\\Audio Watchdog.lnk").c_str()); // v1.0 shortcut
    const std::wstring menu = StartMenuDir();
    CreateDirTree(menu);
    CreateShortcut(menu + L"\\Audio Watchdog.lnk", exe, L"", L"Audio Watchdog");
    CreateShortcut(menu + L"\\Desinstalar Audio Watchdog.lnk", uninstaller, L"", L"Remove o Audio Watchdog");
    CreateShortcut(menu + L"\\Logs do Audio Watchdog.lnk", logs, L"", L"Pasta de logs do Audio Watchdog");
    RegisterUninstallEntry(dir);

    // 6. Start.
    progress(90, L"Iniciando o serviço...");
    if (!StartServiceNow(&err)) {
        error = L"O serviço foi instalado, mas não pôde ser iniciado: " + err;
        SetupLog(dir, error);
        return false;
    }
    if (opt.launchTray) LaunchUnelevated(exe);

    SetupLog(dir, L"Installation completed.");
    progress(100, L"Instalação concluída.");
    return true;
}

// ---------------------------------------------------------------------------
// Uninstall
// ---------------------------------------------------------------------------

bool Uninstall(const std::wstring& dir, bool removeData, std::wstring& error) {
    SetupLog(dir, L"Uninstalling Audio Watchdog.");
    CloseTrayWindows();

    std::wstring err;
    if (!UninstallService(&err)) {
        error = L"Não foi possível remover o serviço: " + err;
        SetupLog(dir, error);
        return false;
    }

    HKEY run = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        ::RegDeleteValueW(run, kProduct);
        ::RegCloseKey(run);
    }
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        ::RegDeleteValueW(run, kProduct);
        ::RegCloseKey(run);
    }
    ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, kUninstallKey);
    ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, kEventLogKey);
    DeleteTree(StartMenuDir());
    ::DeleteFileW((KnownFolder(FOLDERID_Programs) + L"\\Audio Watchdog.lnk").c_str());

    // Trays in other sessions notice the service is gone and exit; give them
    // a moment, then delete (or schedule) the binaries.
    ::Sleep(2500);
    DeleteOrSchedule(dir + L"\\" + kExeName);
    DeleteOrSchedule(dir + L"\\" + kExeName + L".old");
    DeleteOrSchedule(dir + L"\\" + kUninstallerName);

    if (removeData) {
        DeleteTree(dir + L"\\logs");
        DeleteTree(ProgramDataDir());
    } else {
        SetupLog(dir, L"Uninstalled. Logs kept in this folder; configuration kept in " + ProgramDataDir());
    }
    ::RemoveDirectoryW(dir.c_str()); // only succeeds when nothing was kept
    return true;
}

// Uninstall.exe lives in the folder it removes, so it copies itself to %TEMP%
// and continues from there (stage 2), which deletes itself when done.
bool RelaunchFromTemp(const Options& opt, const std::wstring& dir) {
    wchar_t self[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t tmpDir[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tmpDir);
    const std::wstring copy = FormatW(L"%sAudioWatchdog-uninstall-%lu.exe", tmpDir, ::GetCurrentProcessId());
    if (!::CopyFileW(self, copy.c_str(), FALSE)) return false;
    std::wstring cmd = L"\"" + copy + L"\" /uninstall-stage2 /dir=\"" + dir + L"\"";
    if (opt.silent) cmd += L" /S";
    if (opt.removeData) cmd += L" /removedata";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, tmpDir, &si, &pi)) {
        ::DeleteFileW(copy.c_str());
        return false;
    }
    if (opt.silent) ::WaitForSingleObject(pi.hProcess, INFINITE); // keep /S synchronous for scripts
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

// Stage 2 cannot delete itself, and Uninstall.exe may still be running (stage
// 1 waits for stage 2 in silent mode). A detached cmd.exe finishes the job a
// few seconds later: both executables, then the folder if nothing was kept.
// Reboot-time deletion stays scheduled as a fallback.
void DeleteSelfLater(const std::wstring& dir) {
    wchar_t self[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring uninstaller = dir + L"\\" + kUninstallerName;
    ::MoveFileExW(self, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    std::wstring cmd = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & del /f /q \"" + std::wstring(self) +
                       L"\" & del /f /q \"" + uninstaller + L"\" & rmdir \"" + dir + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
}

int RunUninstaller(const Options& opt) {
    std::wstring dir = opt.dir.empty() ? RegReadString(HKEY_LOCAL_MACHINE, kUninstallKey, L"InstallLocation") : opt.dir;
    if (dir.empty()) dir = InstallDir();

    bool removeData = opt.removeData;
    if (!opt.uninstallStage2 && !opt.silent) {
        TASKDIALOGCONFIG tdc{};
        tdc.cbSize = sizeof(tdc);
        tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        tdc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
        tdc.nDefaultButton = IDNO;
        tdc.pszWindowTitle = L"Desinstalar Audio Watchdog";
        tdc.hInstance = ::GetModuleHandleW(nullptr);
        tdc.pszMainIcon = MAKEINTRESOURCEW(IDI_APP);
        tdc.pszMainInstruction = L"Remover o Audio Watchdog deste computador?";
        tdc.pszContent = L"O serviço, a aplicação da bandeja, os atalhos e os arquivos do programa "
                         L"serão removidos. As configurações atuais dos dispositivos de áudio não "
                         L"são revertidas.";
        tdc.pszVerificationText = L"Remover também os logs e a configuração";
        int button = 0;
        BOOL verify = FALSE;
        if (FAILED(::TaskDialogIndirect(&tdc, &button, nullptr, &verify)) || button != IDYES) return 1;
        removeData = verify != FALSE;
    }

    if (!opt.uninstallStage2) {
        Options next = opt;
        next.removeData = removeData;
        if (RelaunchFromTemp(next, dir)) return 0;
        // Could not relaunch: uninstall in place (the uninstaller itself is
        // then scheduled for deletion at reboot).
    }

    std::wstring error;
    const bool ok = Uninstall(dir, removeData, error);
    if (!opt.silent) {
        if (ok) {
            ::MessageBoxW(nullptr, removeData
                              ? L"O Audio Watchdog foi removido."
                              : L"O Audio Watchdog foi removido.\n\nOs logs foram mantidos na pasta de "
                                L"instalação e a configuração em ProgramData.",
                          kTitle, MB_OK | MB_ICONINFORMATION);
        } else {
            ::MessageBoxW(nullptr, error.c_str(), kTitle, MB_OK | MB_ICONERROR);
        }
    }
    if (opt.uninstallStage2) DeleteSelfLater(dir);
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Wizard
// ---------------------------------------------------------------------------

constexpr UINT kMsgProgress = WM_APP + 1; // wParam = percent, lParam = std::wstring*
constexpr UINT kMsgFinished = WM_APP + 2; // wParam = ok, lParam = std::wstring* error

struct WizardState {
    Options opt;
    HWND dlg = nullptr;
    HICON icon = nullptr;
    HFONT titleFont = nullptr;
    bool running = false;
    bool done = false;
};

WizardState g_wiz;

void UpdateFormatControls(HWND dlg) {
    const BOOL on = ::IsDlgButtonChecked(dlg, IDC_SETUP_FORMAT) == BST_CHECKED;
    for (int id : { IDC_SETUP_RATE_LBL, IDC_SETUP_RATE, IDC_SETUP_BITS_LBL, IDC_SETUP_BITS }) {
        ::EnableWindow(::GetDlgItem(dlg, id), on);
    }
}

DWORD WINAPI InstallThread(LPVOID) {
    const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring error;
    const bool ok = Install(g_wiz.opt, [](int pct, const std::wstring& text) {
        ::PostMessageW(g_wiz.dlg, kMsgProgress, static_cast<WPARAM>(pct), reinterpret_cast<LPARAM>(new std::wstring(text)));
    }, error);
    ::PostMessageW(g_wiz.dlg, kMsgFinished, ok ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(error)));
    if (SUCCEEDED(co)) ::CoUninitialize();
    return 0;
}

void BrowseForFolder(HWND dlg) {
    IFileOpenDialog* fd = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd)))) return;
    DWORD flags = 0;
    fd->GetOptions(&flags);
    fd->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    fd->SetTitle(L"Escolha a pasta de instalação");
    if (SUCCEEDED(fd->Show(dlg))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(fd->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                std::wstring chosen = path;
                // Always install into a dedicated folder.
                if (chosen.size() < 15 || chosen.substr(chosen.size() - 14) != kProduct) {
                    if (!chosen.empty() && chosen.back() != L'\\') chosen += L'\\';
                    chosen += kProduct;
                }
                ::SetDlgItemTextW(dlg, IDC_SETUP_DIR, chosen.c_str());
                ::CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    fd->Release();
}

void StartInstall(HWND dlg) {
    wchar_t dir[MAX_PATH] = {};
    ::GetDlgItemTextW(dlg, IDC_SETUP_DIR, dir, MAX_PATH);
    std::wstring d = Trim(dir);
    while (!d.empty() && d.back() == L'\\') d.pop_back();
    if (d.size() < 4 || d[1] != L':') {
        ::MessageBoxW(dlg, L"Informe uma pasta de instalação válida.", kTitle, MB_OK | MB_ICONWARNING);
        return;
    }
    g_wiz.opt.dir = d;
    g_wiz.opt.formatSet = true;
    g_wiz.opt.format = ::IsDlgButtonChecked(dlg, IDC_SETUP_FORMAT) == BST_CHECKED;
    const LRESULT rate = ::SendDlgItemMessageW(dlg, IDC_SETUP_RATE, CB_GETCURSEL, 0, 0);
    const LRESULT bits = ::SendDlgItemMessageW(dlg, IDC_SETUP_BITS, CB_GETCURSEL, 0, 0);
    g_wiz.opt.sampleRate = kAllowedSampleRates[rate == CB_ERR ? 1 : rate];
    g_wiz.opt.bitDepth = kAllowedBitDepths[bits == CB_ERR ? 1 : bits];
    g_wiz.opt.launchTray = ::IsDlgButtonChecked(dlg, IDC_SETUP_LAUNCH) == BST_CHECKED;

    for (int id : { IDC_SETUP_FORMAT, IDC_SETUP_RATE, IDC_SETUP_BITS, IDC_SETUP_DIR, IDC_SETUP_BROWSE,
                    IDC_SETUP_LAUNCH, IDOK, IDCANCEL }) {
        ::EnableWindow(::GetDlgItem(dlg, id), FALSE);
    }
    ::ShowWindow(::GetDlgItem(dlg, IDC_SETUP_PROGRESS), SW_SHOW);
    g_wiz.running = true;
    HANDLE t = ::CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
    if (t) ::CloseHandle(t);
}

INT_PTR CALLBACK SetupDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            g_wiz.dlg = dlg;
            const int size = ::MulDiv(48, static_cast<int>(::GetDpiForWindow(dlg)), 96);
            ::LoadIconWithScaleDown(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), size, size, &g_wiz.icon);
            ::SendDlgItemMessageW(dlg, IDC_SETUP_ICON, STM_SETICON, reinterpret_cast<WPARAM>(g_wiz.icon), 0);
            ::SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_wiz.icon));
            HFONT font = reinterpret_cast<HFONT>(::SendMessageW(dlg, WM_GETFONT, 0, 0));
            LOGFONTW lf{};
            if (font && ::GetObjectW(font, sizeof(lf), &lf)) {
                lf.lfWeight = FW_SEMIBOLD;
                lf.lfHeight = lf.lfHeight * 3 / 2;
                g_wiz.titleFont = ::CreateFontIndirectW(&lf);
                ::SendDlgItemMessageW(dlg, IDC_SETUP_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(g_wiz.titleFont), TRUE);
            }
            const bool upgrade = !RegReadString(HKEY_LOCAL_MACHINE, kUninstallKey, L"InstallLocation").empty();
            ::SetDlgItemTextW(dlg, IDC_SETUP_SUBTITLE,
                              (std::wstring(L"Versão ") + AWW_VERSION_WSTR +
                               (upgrade ? L" — atualização da instalação existente" : L" — instalação")).c_str());

            // Feature 1 is mandatory: checked and locked.
            ::CheckDlgButton(dlg, IDC_SETUP_EXCL, BST_CHECKED);
            ::EnableWindow(::GetDlgItem(dlg, IDC_SETUP_EXCL), FALSE);

            // Feature 2 defaults to the existing configuration (upgrade) or off.
            const Config cfg = LoadConfig(ConfigFilePath(), false);
            ::CheckDlgButton(dlg, IDC_SETUP_FORMAT, cfg.formatStandardization ? BST_CHECKED : BST_UNCHECKED);
            int rateSel = 1;
            for (int i = 0; i < static_cast<int>(std::size(kAllowedSampleRates)); ++i) {
                ::SendDlgItemMessageW(dlg, IDC_SETUP_RATE, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(FormatW(L"%u Hz", kAllowedSampleRates[i]).c_str()));
                if (kAllowedSampleRates[i] == cfg.sampleRate) rateSel = i;
            }
            int bitsSel = 1;
            for (int i = 0; i < static_cast<int>(std::size(kAllowedBitDepths)); ++i) {
                ::SendDlgItemMessageW(dlg, IDC_SETUP_BITS, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(FormatW(L"%u-bit", kAllowedBitDepths[i]).c_str()));
                if (kAllowedBitDepths[i] == cfg.bitDepth) bitsSel = i;
            }
            ::SendDlgItemMessageW(dlg, IDC_SETUP_RATE, CB_SETCURSEL, rateSel, 0);
            ::SendDlgItemMessageW(dlg, IDC_SETUP_BITS, CB_SETCURSEL, bitsSel, 0);
            UpdateFormatControls(dlg);

            ::SetDlgItemTextW(dlg, IDC_SETUP_DIR, g_wiz.opt.dir.c_str());
            ::CheckDlgButton(dlg, IDC_SETUP_LAUNCH, BST_CHECKED);
            ::SendDlgItemMessageW(dlg, IDC_SETUP_PROGRESS, PBM_SETRANGE32, 0, 100);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_SETUP_FORMAT:
                    UpdateFormatControls(dlg);
                    return TRUE;
                case IDC_SETUP_BROWSE:
                    BrowseForFolder(dlg);
                    return TRUE;
                case IDOK:
                    if (g_wiz.done) {
                        ::EndDialog(dlg, 0);
                    } else if (!g_wiz.running) {
                        StartInstall(dlg);
                    }
                    return TRUE;
                case IDCANCEL:
                    if (!g_wiz.running) ::EndDialog(dlg, g_wiz.done ? 0 : 1);
                    return TRUE;
                default:
                    break;
            }
            break;
        case kMsgProgress: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
            ::SendDlgItemMessageW(dlg, IDC_SETUP_PROGRESS, PBM_SETPOS, wParam, 0);
            ::SetDlgItemTextW(dlg, IDC_SETUP_STATUS, text->c_str());
            return TRUE;
        }
        case kMsgFinished: {
            std::unique_ptr<std::wstring> error(reinterpret_cast<std::wstring*>(lParam));
            g_wiz.running = false;
            g_wiz.done = true;
            ::SetDlgItemTextW(dlg, IDOK, L"Concluir");
            ::EnableWindow(::GetDlgItem(dlg, IDOK), TRUE);
            if (wParam) {
                ::SetDlgItemTextW(dlg, IDC_SETUP_STATUS, L"Instalação concluída.");
                const std::wstring text =
                    L"O Audio Watchdog " AWW_VERSION_WSTR L" foi instalado e o serviço está em execução.\n\n"
                    L"Logs: " + g_wiz.opt.dir + L"\\logs";
                ::MessageBoxW(dlg, text.c_str(), kTitle, MB_OK | MB_ICONINFORMATION);
            } else {
                ::SetDlgItemTextW(dlg, IDC_SETUP_STATUS, L"A instalação falhou.");
                ::MessageBoxW(dlg, error->c_str(), kTitle, MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        case WM_CLOSE:
            if (!g_wiz.running) ::EndDialog(dlg, g_wiz.done ? 0 : 1);
            return TRUE;
        case WM_DESTROY:
            if (g_wiz.titleFont) ::DeleteObject(g_wiz.titleFont);
            if (g_wiz.icon) ::DestroyIcon(g_wiz.icon);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

Options ParseCommandLine() {
    Options opt;
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring raw = argv[i];
        const std::wstring a = ToLowerA(raw);
        if (a == L"/s" || a == L"/silent" || a == L"-s") {
            opt.silent = true;
        } else if (a == L"/uninstall") {
            opt.uninstall = true;
        } else if (a == L"/uninstall-stage2") {
            opt.uninstall = true;
            opt.uninstallStage2 = true;
        } else if (a == L"/removedata") {
            opt.removeData = true;
        } else if (a == L"/notray") {
            opt.launchTray = false;
        } else if (a == L"/noformat") {
            opt.formatSet = true;
            opt.format = false;
        } else if (a.rfind(L"/format=", 0) == 0) {
            const std::wstring v = raw.substr(8);
            const size_t colon = v.find(L':');
            const unsigned long rate = std::wcstoul(v.substr(0, colon).c_str(), nullptr, 10);
            const unsigned long bits = colon == std::wstring::npos ? kDefaultBitDepth
                                                                   : std::wcstoul(v.substr(colon + 1).c_str(), nullptr, 10);
            opt.formatSet = true;
            opt.format = true;
            opt.sampleRate = IsAllowedSampleRate(rate) ? rate : kDefaultSampleRate;
            opt.bitDepth = IsAllowedBitDepth(bits) ? static_cast<std::uint16_t>(bits) : kDefaultBitDepth;
        } else if (a.rfind(L"/dir=", 0) == 0) {
            opt.dir = raw.substr(5);
            while (!opt.dir.empty() && (opt.dir.back() == L'\\' || opt.dir.back() == L'"')) opt.dir.pop_back();
        }
    }
    if (argv) ::LocalFree(argv);

    wchar_t self[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring name = ToLowerA(std::wstring(self).substr(std::wstring(self).find_last_of(L'\\') + 1));
    if (name == L"uninstall.exe") opt.uninstall = true;
    return opt;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    ::InitCommonControlsEx(&icc);

    Options opt = ParseCommandLine();
    int rc = 0;
    if (opt.uninstall) {
        rc = RunUninstaller(opt);
    } else {
        if (opt.dir.empty()) opt.dir = DefaultInstallDir();
        if (opt.silent) {
            std::wstring error;
            rc = Install(opt, [](int, const std::wstring&) {}, error) ? 0 : 1;
        } else {
            g_wiz.opt = opt;
            rc = static_cast<int>(::DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SETUP), nullptr, SetupDlgProc, 0));
        }
    }
    Logger::Instance().Flush();
    if (SUCCEEDED(co)) ::CoUninitialize();
    return rc;
}
