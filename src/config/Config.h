#pragma once
// Configuration loaded from a simple INI file under ProgramData.
#ifndef AUDIOWATCHDOG_CONFIG_H
#define AUDIOWATCHDOG_CONFIG_H

#include <string>
#include <cstdint>
#include <map>

#include "logging/Logger.h"

namespace aw {

struct Config {
    // Watch the render (playback) endpoints.
    bool monitorPlayback = true;
    // Watch the capture (recording) endpoints.
    bool monitorCapture = true;
    // How often to re-check endpoints as a fallback (seconds).
    std::uint32_t checkIntervalSeconds = 60;
    // Write a log file.
    bool enableLogging = true;
    // Minimum level to write to the file.
    LogLevel logLevel = LogLevel::Info;
    // If true, the periodic re-check also fixes the exclusive-mode state;
    // if false (diagnostic mode) it only reports.
    bool enforce = true;
    // Disable the exclusive-mode enforcement entirely (leaves the service as
    // a pure monitor). Useful for evaluating whether the process is the
    // culprit in some environment.
    bool exclusiveDisabled = false;
};

// Loads the file at `path` (which may be empty, in which case all defaults are
// used). Unknown keys are ignored; the file is created when missing.
Config LoadConfig(const std::wstring& path);

// Writes the current configuration back (used to scaffold a default file).
bool SaveConfig(const Config& cfg, const std::wstring& path);

} // namespace aw

#endif // AUDIOWATCHDOG_CONFIG_H