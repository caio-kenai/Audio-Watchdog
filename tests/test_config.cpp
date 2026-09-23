#include "tests/test_harness.h"

#include "config/Config.h"
#include "util/StrUtil.h"

#include <cstdio>
#include <fstream>

using namespace aw;

static std::wstring TempConfigPath() {
    return awtest::TempFile(L"aww_test_config.ini");
}

TEST(Config_Defaults) {
    Config cfg; // defaults must match documented behaviour
    CHECK(cfg.monitorPlayback);
    CHECK(cfg.monitorCapture);
    CHECK_EQ(cfg.checkIntervalSeconds, 60u);
    CHECK(cfg.enableLogging);
    CHECK_EQ(cfg.logLevel, LogLevel::Info);
    CHECK(cfg.enforce);
    CHECK(cfg.exclusiveModeProtection);
    CHECK(!cfg.formatStandardization);
    CHECK_EQ(cfg.sampleRate, 48000u);
    CHECK_EQ(cfg.bitDepth, static_cast<std::uint16_t>(24));
}

TEST(Config_WriteReadRoundtrip) {
    const std::wstring path = TempConfigPath();
    ::DeleteFileW(path.c_str());

    Config in;
    in.monitorPlayback = false;
    in.monitorCapture = true;
    in.checkIntervalSeconds = 42;
    in.enableLogging = false;
    in.logLevel = LogLevel::Debug;
    in.enforce = false;
    in.exclusiveModeProtection = false;
    in.formatStandardization = true;
    in.sampleRate = 44100;
    in.bitDepth = 16;
    CHECK(SaveConfig(in, path));

    Config out = LoadConfig(path);
    CHECK_EQ(out.monitorPlayback, false);
    CHECK_EQ(out.monitorCapture, true);
    CHECK_EQ(out.checkIntervalSeconds, 42u);
    CHECK_EQ(out.enableLogging, false);
    CHECK_EQ(out.logLevel, LogLevel::Debug);
    CHECK_EQ(out.enforce, false);
    CHECK_EQ(out.exclusiveModeProtection, false);
    CHECK_EQ(out.formatStandardization, true);
    CHECK_EQ(out.sampleRate, 44100u);
    CHECK_EQ(out.bitDepth, static_cast<std::uint16_t>(16));
    ::DeleteFileW(path.c_str());
}

TEST(Config_UnknownKeysIgnored) {
    const std::wstring path = TempConfigPath();
    ::DeleteFileW(path.c_str());

    {
        std::ofstream f(path, std::ios::out);
        f << "[Monitor]\nPlayback=false\nBogusKey=hello\n";
    }
    Config out = LoadConfig(path);
    CHECK_EQ(out.monitorPlayback, false);
    CHECK_EQ(out.checkIntervalSeconds, 60u); // default untouched
    ::DeleteFileW(path.c_str());
}

TEST(Config_ClampsInterval) {
    const std::wstring path = TempConfigPath();
    std::ofstream f(path, std::ios::out);
    f << "[Monitor]\nCheckIntervalSeconds=500000\n";
    f.close();
    Config out = LoadConfig(path);
    CHECK_EQ(out.checkIntervalSeconds, 86400u); // capped
    ::DeleteFileW(path.c_str());
}

TEST(Config_ParseLevel) {
    CHECK_EQ(ParseLogLevel(L"debug"), LogLevel::Debug);
    CHECK_EQ(ParseLogLevel(L"INFO"), LogLevel::Info);
    CHECK_EQ(ParseLogLevel(L"warn"), LogLevel::Warn);
    CHECK_EQ(ParseLogLevel(L"error"), LogLevel::Error);
    CHECK_EQ(ParseLogLevel(L"bogus"), LogLevel::Info);
}

static Config LoadFromText(const char* text) {
    const std::wstring path = TempConfigPath();
    {
        std::ofstream f(path, std::ios::out | std::ios::trunc);
        f << text;
    }
    Config out = LoadConfig(path, false);
    ::DeleteFileW(path.c_str());
    return out;
}

TEST(Config_RejectsUnsupportedSampleRate) {
    // Only 44100 and 48000 are allowed; anything else falls back to 48000.
    Config out = LoadFromText("[Features]\nFormatStandardization=true\nSampleRate=96000\nBitDepth=32\n");
    CHECK(out.formatStandardization);
    CHECK_EQ(out.sampleRate, 48000u);
    CHECK_EQ(out.bitDepth, static_cast<std::uint16_t>(32));
}

TEST(Config_RejectsUnsupportedBitDepth) {
    Config out = LoadFromText("[Features]\nSampleRate=44100\nBitDepth=20\n");
    CHECK_EQ(out.sampleRate, 44100u);
    CHECK_EQ(out.bitDepth, static_cast<std::uint16_t>(24));
}

TEST(Config_LegacyExclusiveModeDisabledKey) {
    Config out = LoadFromText("[Behavior]\nExclusiveModeDisabled=true\n");
    CHECK(!out.exclusiveModeProtection);
    // The new key wins over the legacy one.
    Config both = LoadFromText("[Features]\nExclusiveModeProtection=true\n[Behavior]\nExclusiveModeDisabled=true\n");
    CHECK(both.exclusiveModeProtection);
}

TEST(Config_InlineCommentsAndCase) {
    Config out = LoadFromText("[monitor]\nplayback=false   ; comment\nCheckIntervalSeconds=15 ; s\n");
    CHECK(!out.monitorPlayback);
    CHECK_EQ(out.checkIntervalSeconds, 15u);
}

TEST(Config_DescribeTarget) {
    Config cfg;
    cfg.sampleRate = 44100;
    cfg.bitDepth = 16;
    CHECK_EQ(DescribeFormatTarget(cfg), std::wstring(L"44100 Hz / 16-bit"));
}
