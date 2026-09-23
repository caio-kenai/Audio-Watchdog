#pragma once
// Thin wrappers around the Windows Service Control Manager (SCM).
#ifndef AUDIOWATCHDOG_SERVICEUTIL_H
#define AUDIOWATCHDOG_SERVICEUTIL_H

#include <string>
#include <windows.h>

namespace aw {

// Internal service name (used with OpenService/CreateService).
constexpr wchar_t kServiceName[] = L"AudioWatchdog";

// Human readable name shown in the Services console.
constexpr wchar_t kServiceDisplayName[] = L"Audio Watchdog";

// Description shown in the Services console.
constexpr wchar_t kServiceDescription[] =
    L"Monitors Windows audio endpoints and prevents applications from using exclusive audio mode.";

// Installs the service. `binaryPath` should include the quoted path to the
// executable plus any optional arguments. Returns true on success.
bool InstallService(const std::wstring& binaryPath, std::wstring* errorOut = nullptr);

// Removes the service. Returns true if it no longer exists (already removed)
// or was successfully deleted.
bool UninstallService(std::wstring* errorOut = nullptr);

// Starts the service (requires elevation).
bool StartServiceNow(std::wstring* errorOut = nullptr);

// Stops the service (requires elevation). Returns true if it was already
// stopped or was stopped successfully.
bool StopService(std::wstring* errorOut = nullptr);

// Convenience: start if stopped, or restart if running.
bool RestartService(std::wstring* errorOut = nullptr);

// Query the current service status. Returns false if the service does not
// exist or the query fails; otherwise fills state/description.
bool QueryServiceStatusExState(DWORD* stateOut, std::wstring* descriptionOut = nullptr);

// Human readable text for a SERVICE_STATUS state.
std::wstring ServiceStateToString(DWORD state);

// Prints a friendly status line including start mode and PID.
bool PrintServiceStatus(std::wstring* errorOut = nullptr);

} // namespace aw

#endif // AUDIOWATCHDOG_SERVICEUTIL_H