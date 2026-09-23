#include "logging/Logger.h"

#include "util/StrUtil.h"
#include "util/Paths.h"

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>

namespace aw {

LogLevel ParseLogLevel(const std::wstring& s) {
    const std::wstring low = ToLowerA(s);
    if (low == L"debug") return LogLevel::Debug;
    if (low == L"info" || low.empty()) return LogLevel::Info;
    if (low == L"warn" || low == L"warning") return LogLevel::Warn;
    if (low == L"error") return LogLevel::Error;
    return LogLevel::Info;
}

std::wstring LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return L"DEBUG";
        case LogLevel::Info: return L"INFO";
        case LogLevel::Warn: return L"WARN";
        case LogLevel::Error: return L"ERROR";
    }
    return L"INFO";
}

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::Configure(const std::wstring& filePath, LogLevel level, bool alsoConsole) {
    std::lock_guard<std::mutex> lk(mutex_);
    filePath_ = filePath;
    level_ = level;
    console_ = alsoConsole;
    fileSize_ = 0;
    OpenFile();
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lk(mutex_);
    level_ = level;
}

void Logger::OpenFile() {
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
    if (filePath_.empty()) return;
    // Plain mode ("a", no ccs): we write UTF-8 bytes ourselves. A ccs=UTF-8
    // stream would crash on byte-oriented std::fwrite (CRT bug/invariant).
    file_ = _wfsopen(filePath_.c_str(), L"a", _SH_DENYNO);
    if (file_) {
        std::fseek(file_, 0, SEEK_END);
        fileSize_ = std::ftell(file_);
    }
}

void Logger::RotateIfNeeded() {
    if (!file_ || fileSize_ < kMaxFileBytes) return;

    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;

    // Shift audiowatchdog.N.log -> audiowatchdog.N+1.log, oldest deleted.
    for (int i = kMaxRotatedFiles - 1; i >= 1; --i) {
        std::wstring src = FormatW(L"%s.%d", filePath_.c_str(), i);
        std::wstring dst = FormatW(L"%s.%d", filePath_.c_str(), i + 1);
        ::DeleteFileW(dst.c_str());
        ::MoveFileW(src.c_str(), dst.c_str());
    }
    std::wstring first = FormatW(L"%s.1", filePath_.c_str());
    ::DeleteFileW(first.c_str());
    ::MoveFileW(filePath_.c_str(), first.c_str());

    fileSize_ = 0;
    OpenFile();
}

void Logger::Log(LogLevel level, const std::wstring& message) {
    if (static_cast<int>(level) < static_cast<int>(level_)) return;

    const std::wstring line = LogLineToString(LogLevelName(level), message);
    const std::string utf8 = Utf8FromWide(line);

    std::lock_guard<std::mutex> lk(mutex_);
    if (console_) {
        FILE* out = level <= LogLevel::Warn ? stdout : stderr;
        std::fputs(utf8.c_str(), out);
        std::fputc('\n', out);
        std::fflush(out);
    }
    if (file_) {
        std::fwrite(utf8.data(), 1, utf8.size(), file_);
        std::fputc('\n', file_);
        fileSize_ += static_cast<std::int64_t>(utf8.size()) + 1;
        std::fflush(file_); // watchdog logs are low volume; keep them live
        RotateIfNeeded();
    }
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (file_) std::fflush(file_);
}

std::wstring LogLineToString(const std::wstring& level, const std::wstring& message) {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t dateBuf[64];
    ::GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"yyyy-MM-dd", dateBuf, 64);
    wchar_t timeBuf[64];
    ::GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &st, L"HH:mm:ss", timeBuf, 64);

    const DWORD pid = ::GetCurrentProcessId();
    const unsigned tid = static_cast<unsigned>(::GetCurrentThreadId());
    return FormatW(L"[%s %s] [%s] [pid %lu tid %u] %s", dateBuf, timeBuf, level.c_str(), pid, tid, message.c_str());
}

} // namespace aw