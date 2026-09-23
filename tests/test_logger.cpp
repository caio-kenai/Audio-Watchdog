#include "tests/test_harness.h"

#include "logging/Logger.h"
#include "util/StrUtil.h"

#include <fstream>
#include <sstream>

using namespace aw;

static std::wstring TempLogPath() {
    return L"C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_log.log";
}

static std::string ReadUtf8File(const std::wstring& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST(Logger_WritesFileWithLevelFiltering) {
    const std::wstring path = TempLogPath();
    ::DeleteFileW(path.c_str());

    Logger& log = Logger::Instance();
    log.Configure(path, LogLevel::Warn, /*alsoConsole=*/false);
    log.Info(L"should not appear");
    log.Warn(L"warning appears");
    log.Error(L"error appears");
    log.Flush();

    const std::string content = ReadUtf8File(path);
    CHECK(content.find("should not appear") == std::string::npos);
    CHECK(content.find("warning appears") != std::string::npos);
    CHECK(content.find("error appears") != std::string::npos);
    ::DeleteFileW(path.c_str());
}

TEST(Logger_LevelNames) {
    CHECK_EQ(LogLevelName(LogLevel::Debug), std::wstring(L"DEBUG"));
    CHECK_EQ(LogLevelName(LogLevel::Info), std::wstring(L"INFO"));
    CHECK_EQ(LogLevelName(LogLevel::Warn), std::wstring(L"WARN"));
    CHECK_EQ(LogLevelName(LogLevel::Error), std::wstring(L"ERROR"));
}