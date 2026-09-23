#pragma once
// Minimal helpers for posting messages to the Windows Application event log.
#ifndef AUDIOWATCHDOG_EVENTLOG_H
#define AUDIOWATCHDOG_EVENTLOG_H

#include <string>
#include <windows.h>

namespace aw {

// Post a message to the Application log under the given source name.
// Safe to call from a console utility (registers the source lazily).
void WriteEventLog(const std::wstring& source, WORD type, const std::wstring& message);

} // namespace aw

#endif // AUDIOWATCHDOG_EVENTLOG_H