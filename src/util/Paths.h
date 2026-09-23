#pragma once
// Resolves well-known paths used by Audio Watchdog.
#ifndef AUDIOWATCHDOG_PATHS_H
#define AUDIOWATCHDOG_PATHS_H

#include <string>

namespace aw {

// Base directory under ProgramData, e.g. C:\ProgramData\Audio Watchdog
std::wstring ProgramDataDir();

// Directory for log files (created on demand).
std::wstring LogsDir();

// Full path of the log file.
std::wstring LogFilePath();

// Full path of the configuration file.
std::wstring ConfigFilePath();

// Create the base directory if it does not exist. Returns false on failure.
bool EnsureProgramDataDirs();

} // namespace aw

#endif // AUDIOWATCHDOG_PATHS_H