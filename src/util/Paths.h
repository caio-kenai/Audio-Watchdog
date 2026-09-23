#pragma once
// Resolves well-known paths used by Audio Watchdog.
#ifndef AUDIOWATCHDOG_PATHS_H
#define AUDIOWATCHDOG_PATHS_H

#include <string>

namespace aw {

// Folder that contains AudioWatchdog.exe (the install folder).
std::wstring InstallDir();

// Full path of the running executable.
std::wstring ExecutablePath();

// Base directory under ProgramData, e.g. C:\ProgramData\Audio Watchdog
std::wstring ProgramDataDir();

// Log folder next to the executable, e.g. C:\Program Files\Audio Watchdog\logs
std::wstring LogsDir();

// Log written by the Windows service (and the CLI / portable tray).
std::wstring LogFilePath();

// Log written by the per-user tray application.
std::wstring TrayLogFilePath();

// Full path of the configuration file (under ProgramData).
std::wstring ConfigFilePath();

// Creates the ProgramData folder. Returns false on failure.
bool EnsureProgramDataDirs();

// Creates the logs folder. Returns false on failure.
bool EnsureLogsDir();

// Applies the folder ACLs used by an installed copy:
//   config folder - SYSTEM/Administrators full control, users read-only
//   logs folder   - SYSTEM/Administrators full control, users modify
// Requires administrator rights; best effort.
bool ApplyConfigDirAcl(const std::wstring& path);
bool ApplyLogsDirAcl(const std::wstring& path);

} // namespace aw

#endif // AUDIOWATCHDOG_PATHS_H
