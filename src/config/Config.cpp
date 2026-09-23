#include "config/Config.h"

#include "util/StrUtil.h"

#include <windows.h>
#include <fstream>
#include <sstream>

namespace aw {

namespace {

bool ParseBool(const std::wstring& v, bool fallback) {
    const std::wstring low = ToLowerA(v);
    if (low == L"true" || low == L"1" || low == L"yes" || low == L"on") return true;
    if (low == L"false" || low == L"0" || low == L"no" || low == L"off") return false;
    return fallback;
}

std::uint32_t ParseU32(const std::wstring& v, std::uint32_t fallback) {
    if (v.empty()) return fallback;
    try {
        unsigned long long n = std::stoull(v);
        const unsigned long long max = 24ull * 3600ull; // cap at 86400 to avoid huge waits
        if (n == 0) n = 1;
        if (n > max) n = max;
        return static_cast<std::uint32_t>(n);
    } catch (...) {
        return fallback;
    }
}

} // namespace

Config LoadConfig(const std::wstring& path) {
    Config cfg;

    std::wifstream in(path, std::ios::in);
    if (!in.is_open()) {
        // Missing file -> defaults. Attempt to scaffold it.
        SaveConfig(cfg, path);
        return cfg;
    }
    in.imbue(std::locale::classic());

    std::wstring section;
    std::wstring line;
    while (std::getline(in, line)) {
        std::wstring s = TrimView(line);
        if (s.empty() || s[0] == L';' || s[0] == L'#') continue;
        if (s.front() == L'[' && s.back() == L']') {
            section = TrimView(s.substr(1, s.size() - 2));
            continue;
        }
        const size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = TrimView(s.substr(0, eq));
        std::wstring value = TrimView(s.substr(eq + 1));
        const std::wstring fullKey = section + L"." + key;

        if (fullKey == L"Monitor.Playback" || fullKey == L"monitor.Playback") {
            cfg.monitorPlayback = ParseBool(value, cfg.monitorPlayback);
        } else if (fullKey == L"Monitor.Capture") {
            cfg.monitorCapture = ParseBool(value, cfg.monitorCapture);
        } else if (fullKey == L"Monitor.CheckIntervalSeconds") {
            cfg.checkIntervalSeconds = ParseU32(value, cfg.checkIntervalSeconds);
        } else if (fullKey == L"Logging.Enable") {
            cfg.enableLogging = ParseBool(value, cfg.enableLogging);
        } else if (fullKey == L"Logging.Level") {
            cfg.logLevel = ParseLogLevel(value);
        } else if (fullKey == L"Behavior.Enforce") {
            cfg.enforce = ParseBool(value, cfg.enforce);
        } else if (fullKey == L"Behavior.ExclusiveModeDisabled") {
            cfg.exclusiveDisabled = ParseBool(value, cfg.exclusiveDisabled);
        }
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

    // Write as UTF-8 for correct display of accents in comments.
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    auto dump = [&](const std::wstring& s) {
        const std::string utf8 = Utf8FromWide(s);
        out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        out.put('\n');
    };

    dump(L"; Audio Watchdog configuration");
    dump(L"; Created automatically on first run. Edit while the service is stopped.");
    dump(L"");
    dump(L"[Monitor]");
    dump(L"Playback=" + (cfg.monitorPlayback ? WString(L"true") : WString(L"false")));
    dump(L"Capture=" + (cfg.monitorCapture ? WString(L"true") : WString(L"false")));
    dump(L"CheckIntervalSeconds=" + std::to_wstring(cfg.checkIntervalSeconds));
    dump(L"");
    dump(L"[Logging]");
    dump(L"Enable=" + (cfg.enableLogging ? WString(L"true") : WString(L"false")));
    dump(L"Level=" + LogLevelName(cfg.logLevel));
    dump(L"");
    dump(L"[Behavior]");
    dump(L"Enforce=" + (cfg.enforce ? WString(L"true") : WString(L"false")));
    dump(L"ExclusiveModeDisabled=" + (cfg.exclusiveDisabled ? WString(L"true") : WString(L"false")));
    out.flush();
    return out.good();
}

} // namespace aw