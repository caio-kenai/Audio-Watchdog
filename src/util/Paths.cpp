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
constexpr wchar_t kConfigFile[] = L"config.ini";
} // namespace

static std::wstring ProgramDataBase() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf);
    }
    // Fallback if SHGetFolderPath fails.
    return L"C:\\ProgramData";
}

static void EnsureDir(const std::wstring& dir) {
    if (!dir.empty()) ::CreateDirectoryW(dir.c_str(), nullptr);
}

// Best-effort: make the folder usable by every authenticated user. Leaves
// SYSTEM/Administrators in full control and lets any normal user read, write
// and delete the logs, even when the tree was first created by an elevated
// installer or by the service account (which would otherwise leave SYSTEM-owned
// files that a user session cannot append to).
static void GrantUsableAcl(const std::wstring& path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;M;;;AU)",
            SDDL_REVISION_1, &sd, nullptr)) {
        return;
    }
    BOOL present = FALSE;
    BOOL daclDefaulted = FALSE;
    PACL dacl = nullptr;
    if (::GetSecurityDescriptorDacl(sd, &present, &dacl, &daclDefaulted) && present) {
        ::SetNamedSecurityInfoW(const_cast<PWSTR>(path.c_str()), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                nullptr, nullptr, dacl, nullptr);
    }
    ::LocalFree(sd);
}

std::wstring ProgramDataDir() {
    return ProgramDataBase() + L"\\" + kBrand;
}

std::wstring LogsDir() {
    return ProgramDataDir() + L"\\" + kLogsSubDir;
}

std::wstring LogFilePath() {
    return LogsDir() + L"\\" + kLogFile;
}

std::wstring ConfigFilePath() {
    return ProgramDataDir() + L"\\" + kConfigFile;
}

bool EnsureProgramDataDirs() {
    EnsureDir(ProgramDataBase());
    EnsureDir(ProgramDataDir());
    EnsureDir(LogsDir());
    GrantUsableAcl(ProgramDataDir());
    GrantUsableAcl(LogsDir());
    const DWORD attr = ::GetFileAttributesW(LogsDir().c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace aw