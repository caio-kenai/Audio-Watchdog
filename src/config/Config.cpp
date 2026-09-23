#include "config/Config.h"

#include "util/StrUtil.h"

#include <windows.h>
#include <fstream>

namespace aw {

namespace {

bool ParseBool(const std::wstring& v, bool fallback) {
    const std::wstring low = ToLowerA(v);
    if (low == L"true" || low == L"1" || low == L"yes" || low == L"on") return true;
    if (low == L"false" || low == L"0" || low == L"no" || low == L"off") return false;
    return fallback;
}

bool ParseNumber(const std::wstring& v, unsigned long long& out) {
    if (v.empty()) return false;
    try {
        size_t used = 0;
        out = std::stoull(v, &used);
        return used == v.size();
    } catch (...) {
        return false;
    }
}

std::uint32_t ParseInterval(const std::wstring& v, std::uint32_t fallback) {
    unsigned long long n = 0;
    if (!ParseNumber(v, n)) return fallback;
    const unsigned long long max = 24ull * 3600ull; // cap at 86400 to avoid huge waits
    if (n == 0) n = 1;
    if (n > max) n = max;
    return static_cast<std::uint32_t>(n);
}

const wchar_t* BoolText(bool b) { return b ? L"true" : L"false"; }

} // namespace

bool IsAllowedSampleRate(std::uint32_t rate) {
    for (std::uint32_t r : kAllowedSampleRates) {
        if (r == rate) return true;
    }
    return false;
}

bool IsAllowedBitDepth(std::uint32_t bits) {
    for (std::uint16_t b : kAllowedBitDepths) {
        if (b == bits) return true;
    }
    return false;
}

std::wstring DescribeFormatTarget(const Config& cfg) {
    return FormatW(L"%u Hz / %u-bit", cfg.sampleRate, static_cast<unsigned>(cfg.bitDepth));
}

Config LoadConfig(const std::wstring& path, bool scaffold) {
    Config cfg;

    std::wifstream in(path, std::ios::in);
    if (!in.is_open()) {
        // Missing file -> defaults. Attempt to scaffold it.
        if (scaffold) SaveConfig(cfg, path);
        return cfg;
    }
    in.imbue(std::locale::classic());

    bool sawProtectionKey = false;
    bool legacyExclusiveDisabled = false;

    std::wstring section;
    std::wstring line;
    while (std::getline(in, line)) {
        std::wstring s = TrimView(line);
        if (s.empty() || s[0] == L';' || s[0] == L'#') continue;
        if (s.front() == L'[' && s.back() == L']') {
            section = ToLowerA(TrimView(s.substr(1, s.size() - 2)));
            continue;
        }
        const size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = ToLowerA(TrimView(s.substr(0, eq)));
        std::wstring value = TrimView(s.substr(eq + 1));
        // Allow trailing inline comments ("Enforce=true ; comment").
        const size_t sc = value.find(L';');
        if (sc != std::wstring::npos) value = TrimView(value.substr(0, sc));
        const std::wstring fullKey = section + L"." + key;

        unsigned long long n = 0;
        if (fullKey == L"monitor.playback") {
            cfg.monitorPlayback = ParseBool(value, cfg.monitorPlayback);
        } else if (fullKey == L"monitor.capture") {
            cfg.monitorCapture = ParseBool(value, cfg.monitorCapture);
        } else if (fullKey == L"monitor.checkintervalseconds") {
            cfg.checkIntervalSeconds = ParseInterval(value, cfg.checkIntervalSeconds);
        } else if (fullKey == L"logging.enable") {
            cfg.enableLogging = ParseBool(value, cfg.enableLogging);
        } else if (fullKey == L"logging.level") {
            cfg.logLevel = ParseLogLevel(value);
        } else if (fullKey == L"behavior.enforce") {
            cfg.enforce = ParseBool(value, cfg.enforce);
        } else if (fullKey == L"behavior.exclusivemodedisabled") {
            // Legacy (v1.0) key, superseded by Features.ExclusiveModeProtection.
            legacyExclusiveDisabled = ParseBool(value, false);
        } else if (fullKey == L"features.exclusivemodeprotection") {
            cfg.exclusiveModeProtection = ParseBool(value, cfg.exclusiveModeProtection);
            sawProtectionKey = true;
        } else if (fullKey == L"features.formatstandardization") {
            cfg.formatStandardization = ParseBool(value, cfg.formatStandardization);
        } else if (fullKey == L"features.samplerate") {
            if (ParseNumber(value, n) && IsAllowedSampleRate(static_cast<std::uint32_t>(n))) {
                cfg.sampleRate = static_cast<std::uint32_t>(n);
            } else {
                Logger::Instance().Warn(L"config: SampleRate=" + value +
                                        L" is not supported (use 44100 or 48000); using 48000.");
            }
        } else if (fullKey == L"features.bitdepth") {
            if (ParseNumber(value, n) && IsAllowedBitDepth(static_cast<std::uint32_t>(n))) {
                cfg.bitDepth = static_cast<std::uint16_t>(n);
            } else {
                Logger::Instance().Warn(L"config: BitDepth=" + value +
                                        L" is not supported (use 16, 24 or 32); using 24.");
            }
        }
    }
    if (!sawProtectionKey && legacyExclusiveDisabled) {
        cfg.exclusiveModeProtection = false;
    }
    return cfg;
}

bool SaveConfig(const Config& cfg, const std::wstring& path) {
    // Ensure the parent directory exists.
    const size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        const std::wstring dir = path.substr(0, slash);
        ::CreateDirectoryW(dir.c_str(), nullptr);
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open()) return false;

    auto dump = [&](const std::wstring& s) {
        const std::string utf8 = Utf8FromWide(s);
        out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        out.write("\r\n", 2);
    };

    dump(L"; Audio Watchdog configuration");
    dump(L"; Changes are picked up when the service restarts or when monitoring is resumed.");
    dump(L"");
    dump(L"[Features]");
    dump(L"; Keep \"Allow applications to take exclusive control\" disabled (main feature).");
    dump(std::wstring(L"ExclusiveModeProtection=") + BoolText(cfg.exclusiveModeProtection));
    dump(L"; Keep every endpoint's default format at SampleRate/BitDepth when supported.");
    dump(std::wstring(L"FormatStandardization=") + BoolText(cfg.formatStandardization));
    dump(L"; 44100 | 48000");
    dump(L"SampleRate=" + std::to_wstring(cfg.sampleRate));
    dump(L"; 16 | 24 | 32");
    dump(L"BitDepth=" + std::to_wstring(cfg.bitDepth));
    dump(L"");
    dump(L"[Monitor]");
    dump(std::wstring(L"Playback=") + BoolText(cfg.monitorPlayback));
    dump(std::wstring(L"Capture=") + BoolText(cfg.monitorCapture));
    dump(L"CheckIntervalSeconds=" + std::to_wstring(cfg.checkIntervalSeconds));
    dump(L"");
    dump(L"[Logging]");
    dump(std::wstring(L"Enable=") + BoolText(cfg.enableLogging));
    dump(L"Level=" + LogLevelName(cfg.logLevel));
    dump(L"");
    dump(L"[Behavior]");
    dump(L"; false = report only, never modify any device.");
    dump(std::wstring(L"Enforce=") + BoolText(cfg.enforce));
    out.flush();
    return out.good();
}

} // namespace aw
