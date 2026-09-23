#pragma once
// Small string/format helpers.
#ifndef AUDIOWATCHDOG_STRUTIL_H
#define AUDIOWATCHDOG_STRUTIL_H

#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace aw {

using WString = std::wstring;
using WStringView = std::wstring_view;

// Wide string <-> narrow string conversions.
std::wstring WideFromUtf8(std::string_view utf8);
std::string Utf8FromWide(std::wstring_view wide);

// Trim whitespace from both ends.
std::wstring TrimView(WStringView s);
std::wstring Trim(const WString& s);

// UTF-8 trim.
std::string TrimUtf8(std::string_view s);

// Lowercase (ASCII fold, locale-independent).
std::wstring ToLowerA(WStringView s);

// Split by an ASCII delimiter.
std::vector<WString> Split(WStringView s, wchar_t delim);

// Format a wide string with printf-style arguments.
WString FormatW(const wchar_t* fmt, ...);

// Hex formatting for HRESULT / DWORD.
std::string HrToHex(HRESULT hr);

// Readable HRESULT text for logs: "0xXXXX (friendly message)".
WString HrText(HRESULT hr);

// System message text for an error code (from FormatMessage).
WString FormatSystemError(DWORD code);

} // namespace aw

#endif // AUDIOWATCHDOG_STRUTIL_H