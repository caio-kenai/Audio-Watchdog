#include "tests/test_harness.h"

#include "config/Config.h"
#include "util/StrUtil.h"

#include <cstdio>
#include <fstream>

using namespace aw;

static std::wstring TempConfigPath() {
    return L"C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_config.ini";
}

TEST(Config_Defaults) {
    Config cfg; // defaults must match documented behaviour
    CHECK(cfg.monitorPlayback);
    CHECK(cfg.monitorCapture);
    CHECK_EQ(cfg.checkIntervalSeconds, 60u);
    CHECK(cfg.enableLogging);
    CHECK_EQ(cfg.logLevel, LogLevel::Info);
    CHECK(cfg.enforce);
    CHECK(!cfg.exclusiveDisabled);
}

TEST(Config_WriteReadRoundtrip) {
    const std::wstring path = TempConfigPath();
    std::remove("C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_config.ini");

    Config in;
    in.monitorPlayback = false;
    in.monitorCapture = true;
    in.checkIntervalSeconds = 42;
    in.enableLogging = false;
    in.logLevel = LogLevel::Debug;
    in.enforce = false;
    in.exclusiveDisabled = true;
    CHECK(SaveConfig(in, path));

    Config out = LoadConfig(path);
    CHECK_EQ(out.monitorPlayback, false);
    CHECK_EQ(out.monitorCapture, true);
    CHECK_EQ(out.checkIntervalSeconds, 42u);
    CHECK_EQ(out.enableLogging, false);
    CHECK_EQ(out.logLevel, LogLevel::Debug);
    CHECK_EQ(out.enforce, false);
    CHECK_EQ(out.exclusiveDisabled, true);
    std::remove("C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_config.ini");
}

TEST(Config_UnknownKeysIgnored) {
    const std::wstring path = TempConfigPath();
    std::remove("C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_config.ini");

    {
        std::ofstream f(path, std::ios::out);
        f << "[Monitor]\nPlayback=false\nBogusKey=hello\n";
    }
    Config out = LoadConfig(path);
    CHECK_EQ(out.monitorPlayback, false);
    CHECK_EQ(out.checkIntervalSeconds, 60u); // default untouched
    std::remove("C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_config.ini");
}

TEST(Config_ClampsInterval) {
    const std::wstring path = TempConfigPath();
    std::ofstream f(path, std::ios::out);
    f << "[Monitor]\nCheckIntervalSeconds=500000\n";
    f.close();
    Config out = LoadConfig(path);
    CHECK_EQ(out.checkIntervalSeconds, 86400u); // capped
    std::remove("C:\\Users\\CAIO_DEV\\AppData\\Local\\Temp\\opencode\\aww_test_config.ini");
}

TEST(Config_ParseLevel) {
    CHECK_EQ(ParseLogLevel(L"debug"), LogLevel::Debug);
    CHECK_EQ(ParseLogLevel(L"INFO"), LogLevel::Info);
    CHECK_EQ(ParseLogLevel(L"warn"), LogLevel::Warn);
    CHECK_EQ(ParseLogLevel(L"error"), LogLevel::Error);
    CHECK_EQ(ParseLogLevel(L"bogus"), LogLevel::Info);
}