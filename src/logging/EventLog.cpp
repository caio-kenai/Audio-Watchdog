#include "logging/EventLog.h"

#include "util/StrUtil.h"

void aw::WriteEventLog(const std::wstring& source, WORD type, const std::wstring& message) {
    HANDLE h = ::RegisterEventSourceW(nullptr, source.c_str());
    if (!h) return;
    const wchar_t* strings[] = { message.c_str() };
    ::ReportEventW(h, type, 0, 0, nullptr, 1, 0, strings, nullptr);
    ::DeregisterEventSource(h);
}