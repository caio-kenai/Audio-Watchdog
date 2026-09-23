#include "util/Paths.h"

#include "util/StrUtil.h"

#include <windows.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>

namespace aw {

namespace {
constexpr wchar_t kBrand[] = L"Audio Watchdog";
constexpr wchar_t kLogsSubDir[] = L"logs";
constexpr wchar_t kLogFile[] = L"audiowatchdog.log";
constexpr wchar_t kTrayLogFile[] = L"tray.log";
constexpr wchar_t kConfigFile[] = L"config.ini";

std::wstring ProgramDataBase() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf);
    }
    // Fallback if SHGetFolderPath fails.
    return L"C:\\ProgramData";
}

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    if (::CreateDirectoryW(dir.c_str(), nullptr)) return true;
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ApplySddl(const std::wstring& path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }
    BOOL present = FALSE;
    BOOL daclDefaulted = FALSE;
    PACL dacl = nullptr;
    bool ok = false;
    if (::GetSecurityDescriptorDacl(sd, &present, &dacl, &daclDefaulted) && present) {
        ok = ::SetNamedSecurityInfoW(const_cast<PWSTR>(path.c_str()), SE_FILE_OBJECT,
                                     DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                     nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
    }
    ::LocalFree(sd);
    return ok;
}

} // namespace

std::wstring ExecutablePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return L"";
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2); // long path
    }
}

std::wstring InstallDir() {
    const std::wstring exe = ExecutablePath();
    const size_t slash = exe.find_last_of(L'\\');
    return slash == std::wstring::npos ? exe : exe.substr(0, slash);
}

std::wstring ProgramDataDir() {
    return ProgramDataBase() + L"\\" + kBrand;
}

std::wstring LogsDir() {
    return InstallDir() + L"\\" + kLogsSubDir;
}

std::wstring LogFilePath() {
    return LogsDir() + L"\\" + kLogFile;
}

std::wstring TrayLogFilePath() {
    return LogsDir() + L"\\" + kTrayLogFile;
}

std::wstring ConfigFilePath() {
    return ProgramDataDir() + L"\\" + kConfigFile;
}

bool EnsureProgramDataDirs() {
    return EnsureDir(ProgramDataDir());
}

bool EnsureLogsDir() {
    return EnsureDir(LogsDir());
}

bool ApplyConfigDirAcl(const std::wstring& path) {
    return ApplySddl(path, L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;AU)");
}

bool ApplyLogsDirAcl(const std::wstring& path) {
    // 0x1301bf = "Modify" (read, write, execute, delete) for authenticated users.
    return ApplySddl(path, L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;AU)");
}

} // namespace aw
