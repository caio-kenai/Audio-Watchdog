#pragma once
// Windows service entry points for "Audio Watchdog".
#ifndef AUDIOWATCHDOG_SERVICE_H
#define AUDIOWATCHDOG_SERVICE_H

#include <windows.h>

namespace aw {

// Called from main() when running as a service (--service). Registers the
// service table and blocks until SCM stops us.
void RunAsService();

// Debug/console mode: runs the watchdog loop in the foreground until
// Ctrl+C / Ctrl+Break. Used with `--foreground`.
void RunForeground();

} // namespace aw

#endif // AUDIOWATCHDOG_SERVICE_H