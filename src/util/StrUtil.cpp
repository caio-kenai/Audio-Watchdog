#include "util/StrUtil.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace aw {

std::wstring WideFromUtf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = static_cast<int>(utf8.size());
    const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), len, nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out;
    out.resize(static_cast<size_t>(needed));
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), len, out.data(), needed);
    return out;
}

std::string Utf8FromWide(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int len = static_cast<int>(wide.size());
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out;
    out.resize(static_cast<size_t>(needed));
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), len, out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring TrimView(WStringView s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    return std::wstring(s.substr(b, e - b));
}

std::wstring Trim(const WString& s) { return TrimView(s); }

std::string TrimUtf8(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return std::string(s.substr(b, e - b));
}

std::wstring ToLowerA(WStringView s) {
    std::wstring out(s);
    for (auto& ch : out) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return out;
}

std::vector<WString> Split(WStringView s, wchar_t delim) {
    std::vector<WString> parts;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delim) {
            parts.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::wstring FormatW(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int needed = ::vswprintf(nullptr, 0, fmt, args);
    va_end(args);
    if (needed < 0) return L"";
    std::wstring out;
    out.resize(static_cast<size_t>(needed) + 1);
    va_start(args, fmt);
    ::vswprintf(out.data(), out.size(), fmt, args);
    va_end(args);
    out.resize(static_cast<size_t>(needed));
    return out;
}

std::string HrToHex(HRESULT hr) {
    wchar_t buf[24];
    ::swprintf(buf, 24, L"0x%08X", static_cast<unsigned>(hr));
    return Utf8FromWide(buf);
}

std::wstring HrText(HRESULT hr) {
    wchar_t* buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD n = ::FormatMessageW(flags, nullptr, static_cast<DWORD>(hr),
                                     0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring system = (n > 0 && buf) ? TrimView(buf) : std::wstring();
    if (buf) ::LocalFree(buf);
    const std::wstring hex = FormatW(L"0x%08X", static_cast<unsigned>(hr));
    if (system.empty()) return hex;
    return hex + L" (" + system + L")";
}

std::wstring FormatSystemError(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD n = ::FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    if (n == 0 || buf == nullptr) return FormatW(L"error %u", code);
    std::wstring msg(buf);
    ::LocalFree(buf);
    msg = TrimView(msg);
    return msg;
}

} // namespace aw