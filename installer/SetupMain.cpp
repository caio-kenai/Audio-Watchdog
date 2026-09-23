// Audio Watchdog Setup
// Self-contained Win32 installer: embeds AudioWatchdog.exe as an RCDATA
// resource, installs it under Program Files, creates the ProgramData logs
// folder with a usable ACL, registers auto-start and a Start Menu shortcut,
// then launches the tray app.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>

#include "setup_res.h"

#include <string>

namespace {

std::wstring WideFromPath(const wchar_t* buf, DWORD len) {
    if (len == 0) return L"";
    return std::wstring(buf, len);
}

std::wstring SystemDir(int csidl) {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf);
    }
    return L"";
}

bool EnsureDir(const std::wstring& path) {
    return ::CreateDirectoryW(path.c_str(), nullptr) != FALSE || ::GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring LastErrorMessage(DWORD code) {
    wchar_t* msg = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring text = (n > 0) ? std::wstring(msg, n) : L"error " + std::to_wstring(code);
    if (msg) ::LocalFree(msg);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
    return text;
}

bool GrantUsableAcl(const std::wstring& path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;M;;;AU)",
            SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }
    BOOL present = FALSE, dflt = FALSE;
    PACL dacl = nullptr;
    if (!::GetSecurityDescriptorDacl(sd, &present, &dacl, &dflt)) {
        ::LocalFree(sd);
        return false;
    }
    ::SetNamedSecurityInfoW(const_cast<PWSTR>(path.c_str()), SE_FILE_OBJECT,
                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, dacl, nullptr);
    ::LocalFree(sd);
    return true;
}

bool WriteDefaultConfig(const std::wstring& path) {
    if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true; // keep existing
    static const char content[] =
        "[Monitor]\r\n"
        "Playback=true\r\n"
        "Capture=true\r\n"
        "CheckIntervalSeconds=60\r\n"
        "\r\n"
        "[Logging]\r\n"
        "Enable=true\r\n"
        "Level=INFO\r\n"
        "\r\n"
        "[Behavior]\r\n"
        "Enforce=true\r\n"
        "ExclusiveModeDisabled=false\r\n";
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    ::WriteFile(h, content, static_cast<DWORD>(sizeof(content) - 1), &written, nullptr);
    ::CloseHandle(h);
    return written == sizeof(content) - 1;
}

bool SetAutoStart(const std::wstring& exePath) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring cmd = L"\"" + exePath + L"\"";
    const DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
    const LONG r = ::RegSetValueExW(key, L"Audio Watchdog", 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(cmd.c_str()), bytes);
    ::RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool CreateStartMenuShortcut(const std::wstring& exePath) {
    const std::wstring programs = SystemDir(CSIDL_PROGRAMS);
    if (programs.empty()) return false;
    const std::wstring lnk = programs + L"\\Audio Watchdog.lnk";

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&link)))) {
        link->SetPath(exePath.c_str());
        link->SetWorkingDirectory(exePath.substr(0, exePath.find_last_of(L'\\') + 1).c_str());
        link->SetDescription(L"Audio Watchdog - keeps Windows audio exclusive mode disabled");
        link->SetIconLocation(exePath.c_str(), 0);
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
            ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
            pf->Release();
        }
        link->Release();
    }
    ::CoUninitialize();
    return ok;
}

void ShowError(const std::wstring& step, DWORD code) {
    std::wstring msg = L"Audio Watchdog setup failed at: " + step + L"\n\n" + LastErrorMessage(code);
    ::MessageBoxW(nullptr, msg.c_str(), L"Audio Watchdog Setup", MB_OK | MB_ICONERROR);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    const std::wstring programFiles = SystemDir(CSIDL_PROGRAM_FILES);
    const std::wstring commonAppData = SystemDir(CSIDL_COMMON_APPDATA);
    if (programFiles.empty() || commonAppData.empty()) {
        ShowError(L"resolve well-known folders", ::GetLastError());
        return 1;
    }

    const std::wstring installDir = programFiles + L"\\Audio Watchdog";
    const std::wstring exePath = installDir + L"\\AudioWatchdog.exe";
    const std::wstring appData = commonAppData + L"\\Audio Watchdog";
    const std::wstring logsDir = appData + L"\\logs";
    const std::wstring configPath = appData + L"\\config.ini";

    // 1. Extract the embedded payload (AudioWatchdog.exe).
    HRSRC res = ::FindResourceW(hInstance, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
    if (!res) { ShowError(L"find embedded payload", ::GetLastError()); return 1; }
    HGLOBAL hg = ::LoadResource(hInstance, res);
    if (!hg) { ShowError(L"load embedded payload", ::GetLastError()); return 1; }
    const void* data = ::LockResource(hg);
    const DWORD size = ::SizeofResource(hInstance, res);
    if (!data || size == 0) { ShowError(L"read embedded payload", ERROR_INVALID_DATA); return 1; }

    if (::GetFileAttributesW(installDir.c_str()) == INVALID_FILE_ATTRIBUTES && !EnsureDir(installDir)) {
        ShowError(L"create install directory", ::GetLastError());
        return 1;
    }
    ::DeleteFileW(exePath.c_str()); // allow replacing a running binary's file
    HANDLE hFile = ::CreateFileW(exePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { ShowError(L"write AudioWatchdog.exe", ::GetLastError()); return 1; }
    DWORD writtenTotal = 0;
    BOOL ok = TRUE;
    while (ok && writtenTotal < size) {
        DWORD chunk = size - writtenTotal;
        DWORD written = 0;
        ok = ::WriteFile(hFile, static_cast<const BYTE*>(data) + writtenTotal, chunk, &written, nullptr);
        writtenTotal += written;
    }
    ::CloseHandle(hFile);
    if (!ok) { ShowError(L"write AudioWatchdog.exe (incomplete)", ::GetLastError()); return 1; }

    // 2. ProgramData folders + ACL.
    if (!EnsureDir(appData)) { ShowError(L"create ProgramData folder", ::GetLastError()); return 1; }
    if (!EnsureDir(logsDir)) { ShowError(L"create logs folder", ::GetLastError()); return 1; }
    GrantUsableAcl(appData);
    GrantUsableAcl(logsDir);

    // 3. Default config.
    WriteDefaultConfig(configPath);

    // 4. Auto-start at logon + Start Menu shortcut.
    SetAutoStart(exePath);
    CreateStartMenuShortcut(exePath);

    // 5. Write a setup log.
    {
        std::wstring line = L"Audio Watchdog installed: " + std::to_wstring(::GetTickCount64()) +
                            L"\r\nBinary: " + exePath + L"\r\nLogs:   " + logsDir + L"\r\n";
        HANDLE h = ::CreateFileW((logsDir + L"\\setup.log").c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const std::wstring wide = line;
            const int wlen = static_cast<int>(wide.size());
            const int u8 = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wlen, nullptr, 0, nullptr, nullptr);
            std::string utf8(u8, '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wlen, &utf8[0], u8, nullptr, nullptr);
            DWORD w2 = 0;
            ::WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &w2, nullptr);
            ::CloseHandle(h);
        }
    }

    // 6. Launch the tray app.
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(exePath.c_str(), const_cast<LPWSTR>(exePath.c_str()),
                         nullptr, nullptr, FALSE, 0, nullptr,
                         installDir.c_str(), &si, &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }

    std::wstring msg = L"Audio Watchdog installed successfully.\n\n"
                       L"Logs are written to:\n" + logsDir + L"\n\n"
                       L"It is now running in the notification area (system tray).";
    ::MessageBoxW(nullptr, msg.c_str(), L"Audio Watchdog Setup", MB_OK | MB_ICONINFORMATION);
    return 0;
}