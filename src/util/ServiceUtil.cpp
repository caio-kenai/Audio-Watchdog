#include "util/ServiceUtil.h"

#include "util/StrUtil.h"

#include <windows.h>
#include <winsvc.h>
#include <sddl.h>
#include <iostream>

namespace aw {

namespace {

constexpr DWORD kBufferNeeded = 6 * 1024;

SC_HANDLE OpenScmManager(DWORD access) {
    return ::OpenSCManagerW(nullptr, nullptr, access);
}

SC_HANDLE OpenServiceHandle(const wchar_t* service, DWORD access) {
    SC_HANDLE scm = OpenScmManager(SC_MANAGER_CONNECT);
    if (!scm) return nullptr;
    SC_HANDLE s = ::OpenServiceW(scm, service, access);
    ::CloseServiceHandle(scm);
    return s;
}

} // namespace

bool InstallService(const std::wstring& binaryPath, std::wstring* errorOut) {
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        if (errorOut) *errorOut = FormatSystemError(::GetLastError());
        return false;
    }

    SC_HANDLE svc = ::OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
    if (svc) {
        // Upgrade in place: keep the registration, refresh its configuration.
        if (!::ChangeServiceConfigW(svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                                    SERVICE_ERROR_NORMAL, binaryPath.c_str(), nullptr, nullptr,
                                    L"\0", L"LocalSystem", nullptr, kServiceDisplayName)) {
            if (errorOut) *errorOut = FormatSystemError(::GetLastError());
            ::CloseServiceHandle(svc);
            ::CloseServiceHandle(scm);
            return false;
        }
    } else {
        const DWORD err = ::GetLastError();
        if (err != ERROR_SERVICE_DOES_NOT_EXIST) {
            if (errorOut) *errorOut = FormatSystemError(err);
            ::CloseServiceHandle(scm);
            return false;
        }
        // No dependencies: the audio services may start later during boot;
        // the engine retries enumeration until they are reachable.
        svc = ::CreateServiceW(scm, kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
                               SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                               binaryPath.c_str(), nullptr, nullptr, nullptr, nullptr, L"LocalSystem");
        if (!svc) {
            if (errorOut) *errorOut = FormatSystemError(::GetLastError());
            ::CloseServiceHandle(scm);
            return false;
        }
    }

    SERVICE_DESCRIPTIONW desc{};
    desc.lpDescription = const_cast<wchar_t*>(kServiceDescription);
    ::ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    // Recovery: restart the service on the first, second and subsequent
    // failures; the failure counter resets after one day without failures.
    SC_ACTION actions[3] = {};
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 5000;
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 10000;
    actions[2].Type = SC_ACTION_RESTART;
    actions[2].Delay = 30000;
    SERVICE_FAILURE_ACTIONSW failure{};
    failure.dwResetPeriod = 86400;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ::ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    // Also restart when the service stops itself with an error exit code
    // (not only on crashes). A clean stop (exit code 0) is never restarted,
    // which is what the tray's "Encerrar" relies on.
    SERVICE_FAILURE_ACTIONS_FLAG flag{};
    flag.fFailureActionsOnNonCrashFailures = TRUE;
    ::ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag);

    // Default service DACL plus start/stop/pause-continue for interactive
    // users, so the tray icon can pause, resume and exit without elevation.
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)"
            L"(A;;CCLCSWRPWPDTLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)",
            SDDL_REVISION_1, &sd, nullptr)) {
        ::SetServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, sd);
        ::LocalFree(sd);
    }

    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);
    return true;
}

bool UninstallService(std::wstring* errorOut) {
    SC_HANDLE svc = OpenServiceHandle(kServiceName, SERVICE_ALL_ACCESS);
    if (!svc) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) return true; // nothing to remove
        if (errorOut) *errorOut = FormatSystemError(err);
        return false;
    }
    SERVICE_STATUS status{};
    // Try to stop it first if it is running.
    if (::ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        for (int i = 0; i < 40 && status.dwCurrentState != SERVICE_STOPPED; ++i) {
            ::Sleep(500);
            ::QueryServiceStatus(svc, &status);
        }
    }
    const bool ok = ::DeleteService(svc) != FALSE;
    if (!ok && errorOut) *errorOut = FormatSystemError(::GetLastError());
    ::CloseServiceHandle(svc);
    return ok;
}

bool StartServiceNow(std::wstring* errorOut) {
    SC_HANDLE svc = OpenServiceHandle(kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        if (errorOut) *errorOut = FormatSystemError(::GetLastError());
        return false;
    }
    bool ok = ::StartServiceW(svc, 0, nullptr) != FALSE;
    if (!ok && errorOut) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) ok = true;
        else *errorOut = FormatSystemError(err);
    }
    ::CloseServiceHandle(svc);
    return ok;
}

bool StopService(std::wstring* errorOut) {
    SC_HANDLE svc = OpenServiceHandle(kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        if (errorOut) *errorOut = FormatSystemError(::GetLastError());
        return false;
    }
    SERVICE_STATUS status{};
    bool ok = ::ControlService(svc, SERVICE_CONTROL_STOP, &status) != FALSE;
    if (!ok) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            ok = true;
        } else if (errorOut) {
            *errorOut = FormatSystemError(err);
        }
    } else {
        for (int i = 0; i < 40 && status.dwCurrentState != SERVICE_STOPPED; ++i) {
            ::Sleep(500);
            ::QueryServiceStatus(svc, &status);
        }
    }
    ::CloseServiceHandle(svc);
    return ok;
}

bool RestartService(std::wstring* errorOut) {
    if (!StopService(errorOut)) return false;
    return StartServiceNow(errorOut);
}

namespace {

bool SendAndWait(DWORD control, DWORD targetState, std::wstring* errorOut) {
    SC_HANDLE svc = OpenServiceHandle(kServiceName, SERVICE_PAUSE_CONTINUE | SERVICE_QUERY_STATUS);
    if (!svc) {
        if (errorOut) *errorOut = FormatSystemError(::GetLastError());
        return false;
    }
    SERVICE_STATUS status{};
    bool ok = ::ControlService(svc, control, &status) != FALSE;
    if (!ok) {
        if (errorOut) *errorOut = FormatSystemError(::GetLastError());
    } else {
        for (int i = 0; i < 40 && status.dwCurrentState != targetState; ++i) {
            ::Sleep(250);
            if (!::QueryServiceStatus(svc, &status)) break;
        }
        ok = status.dwCurrentState == targetState;
        if (!ok && errorOut) *errorOut = L"The service did not reach the requested state.";
    }
    ::CloseServiceHandle(svc);
    return ok;
}

} // namespace

bool PauseService(std::wstring* errorOut) {
    return SendAndWait(SERVICE_CONTROL_PAUSE, SERVICE_PAUSED, errorOut);
}

bool ContinueService(std::wstring* errorOut) {
    return SendAndWait(SERVICE_CONTROL_CONTINUE, SERVICE_RUNNING, errorOut);
}

bool ServiceExists() {
    SC_HANDLE svc = OpenServiceHandle(kServiceName, SERVICE_QUERY_STATUS);
    if (!svc) return false;
    ::CloseServiceHandle(svc);
    return true;
}

bool QueryServiceStatusExState(DWORD* stateOut, std::wstring* descriptionOut) {
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = ::OpenServiceW(scm, kServiceName, SERVICE_QUERY_STATUS);
    if (!svc) {
        ::CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS status{};
    const bool ok = ::QueryServiceStatus(svc, &status) != FALSE;

    if (descriptionOut) {
        DWORD needed = 0;
        ::QueryServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &needed);
        if (needed > 0) {
            std::wstring buf(needed / sizeof(wchar_t) + 2, L'\0');
            DWORD got = 0;
            if (::QueryServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION,
                                       reinterpret_cast<LPBYTE>(buf.data()),
                                       static_cast<DWORD>(buf.size() * sizeof(wchar_t)),
                                       &got)) {
                auto* desc = reinterpret_cast<SERVICE_DESCRIPTIONW*>(buf.data());
                if (desc->lpDescription) *descriptionOut = desc->lpDescription;
            }
        }
    }

    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);
    if (ok && stateOut) *stateOut = status.dwCurrentState;
    return ok;
}

std::wstring ServiceStateToString(DWORD state) {
    switch (state) {
        case SERVICE_STOPPED: return L"Stopped";
        case SERVICE_START_PENDING: return L"Starting";
        case SERVICE_STOP_PENDING: return L"Stopping";
        case SERVICE_RUNNING: return L"Running";
        case SERVICE_CONTINUE_PENDING: return L"Continuing";
        case SERVICE_PAUSE_PENDING: return L"Pausing";
        case SERVICE_PAUSED: return L"Paused";
        default: return FormatW(L"Unknown(%u)", state);
    }
}

bool PrintServiceStatus(std::wstring* errorOut) {
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        if (errorOut) *errorOut = FormatSystemError(::GetLastError());
        return false;
    }
    SC_HANDLE svc = ::OpenServiceW(scm, kServiceName, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!svc) {
        const DWORD err = ::GetLastError();
        ::CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::wcout << L"Audio Watchdog is NOT installed.\n";
            if (errorOut) *errorOut = L"Not installed";
            return false;
        }
        if (errorOut) *errorOut = FormatSystemError(err);
        return false;
    }

    SERVICE_STATUS_PROCESS statusProc{};
    DWORD bytesNeeded = 0;
    const bool statusOk = ::QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                                                 reinterpret_cast<LPBYTE>(&statusProc),
                                                 sizeof(statusProc), &bytesNeeded) != FALSE;

    QUERY_SERVICE_CONFIGW* cfg = nullptr;
    DWORD needed = 0;
    ::QueryServiceConfigW(svc, nullptr, 0, &needed);
    std::wstring buf;
    if (needed > 0) {
        buf.resize(needed);
        cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
        ::QueryServiceConfigW(svc, cfg, needed, &needed);
    }

    std::wcout << L"Service          : " << kServiceDisplayName << L" (" << kServiceName << L")\n";
    if (statusOk) {
        std::wcout << L"State            : " << ServiceStateToString(statusProc.dwCurrentState) << L"\n";
        std::wcout << L"Process ID       : "
                   << (statusProc.dwProcessId != 0 ? FormatW(L"%lu", statusProc.dwProcessId) : L"(not running)") << L"\n";
        if (statusProc.dwProcessId != 0) {
            std::wcout << L"Accept           :";
            if (statusProc.dwControlsAccepted & SERVICE_ACCEPT_STOP) std::wcout << L" STOP";
            if (statusProc.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN) std::wcout << L" SHUTDOWN";
            if (statusProc.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN) std::wcout << L" PRESHUTDOWN";
            if (statusProc.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) std::wcout << L" PAUSE_CONTINUE";
            std::wcout << L"\n";
        }
    }
    if (cfg) {
        std::wcout << L"Start type       : ";
        switch (cfg->dwStartType) {
            case SERVICE_AUTO_START: std::wcout << L"Automatic"; break;
            case SERVICE_DEMAND_START: std::wcout << L"Manual"; break;
            case SERVICE_DISABLED: std::wcout << L"Disabled"; break;
            default: std::wcout << L"Other(" << cfg->dwStartType << L")"; break;
        }
        std::wcout << L"\n";
    }

    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);
    return true;
}

} // namespace aw