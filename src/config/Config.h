#pragma once
// Configuration loaded from a simple INI file under ProgramData.
#ifndef AUDIOWATCHDOG_CONFIG_H
#define AUDIOWATCHDOG_CONFIG_H

#include <string>
#include <cstdint>

#include "logging/Logger.h"

namespace aw {

// Sample rates the format standardization may target. Limited on purpose to
// the two rates virtually every endpoint supports.
constexpr std::uint32_t kAllowedSampleRates[] = { 44100, 48000 };
constexpr std::uint16_t kAllowedBitDepths[] = { 16, 24, 32 };
constexpr std::uint32_t kDefaultSampleRate = 48000;
constexpr std::uint16_t kDefaultBitDepth = 24;

bool IsAllowedSampleRate(std::uint32_t rate);
bool IsAllowedBitDepth(std::uint32_t bits);

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
    // Global dry-run switch: when false nothing is ever modified, the
    // watchdog only reports what it would change.
    bool enforce = true;

    // Feature 1 - keep "Allow applications to take exclusive control" off.
    bool exclusiveModeProtection = true;

    // Feature 2 (optional) - keep every endpoint's default format at
    // sampleRate / bitDepth when the hardware supports it.
    bool formatStandardization = false;
    std::uint32_t sampleRate = kDefaultSampleRate;
    std::uint16_t bitDepth = kDefaultBitDepth;
};

// Loads the file at `path`. Unknown keys are ignored, invalid values fall back
// to their defaults, and the file is created when missing (when `scaffold`).
Config LoadConfig(const std::wstring& path, bool scaffold = true);

// Writes the configuration (used to scaffold a default file and by setup).
bool SaveConfig(const Config& cfg, const std::wstring& path);

// "48000 Hz / 24-bit" style description of the format target.
std::wstring DescribeFormatTarget(const Config& cfg);

} // namespace aw

#endif // AUDIOWATCHDOG_CONFIG_H
