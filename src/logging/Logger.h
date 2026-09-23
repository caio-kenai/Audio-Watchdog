#pragma once
// Filesystem logger with level filtering and size-based rotation.
#ifndef AUDIOWATCHDOG_LOGGER_H
#define AUDIOWATCHDOG_LOGGER_H

#include <atomic>
#include <string>
#include <mutex>
#include <cstdio>
#include <cstdint>
#include <chrono>

#include "util/Win.h"

namespace aw {

enum class LogLevel : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

LogLevel ParseLogLevel(const std::wstring& s);
std::wstring LogLevelName(LogLevel level);

// Thread-safe singleton logger. Writes to a file under ProgramData and,
// optionally, to stdout when running interactively.
class Logger {
public:
    static Logger& Instance();

    void Configure(const std::wstring& filePath, LogLevel level, bool alsoConsole);
    void SetLevel(LogLevel level);

    void Log(LogLevel level, const std::wstring& message);

    // Convenience helpers.
    void Debug(const std::wstring& msg) { Log(LogLevel::Debug, msg); }
    void Info(const std::wstring& msg) { Log(LogLevel::Info, msg); }
    void Warn(const std::wstring& msg) { Log(LogLevel::Warn, msg); }
    void Error(const std::wstring& msg) { Log(LogLevel::Error, msg); }

    // flushes any pending write
    void Flush();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void RotateIfNeeded();
    void OpenFile();

    std::mutex       mutex_;
    std::wstring     filePath_;
    std::atomic<LogLevel> level_{LogLevel::Info};
    bool             console_ = false;
    FILE*            file_ = nullptr;
    std::int64_t     fileSize_ = 0;
    std::int64_t     rotateAt_ = kMaxFileBytes; // raised when a rotation fails
    static constexpr std::int64_t kMaxFileBytes = 5 * 1024 * 1024;   // 5 MB
    static constexpr int          kMaxRotatedFiles = 5;              // keep 5 old copies
};

// Free helper that returns a timestamped, leveled line.
std::wstring LogLineToString(const std::wstring& level, const std::wstring& message);

} // namespace aw

#endif // AUDIOWATCHDOG_LOGGER_H