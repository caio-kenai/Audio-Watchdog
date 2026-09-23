#pragma once
// System-tray resident mode: shows an icon in the notification area, runs the
// watchdog engine and device watcher, writes logs, and offers a context menu.
#ifndef AUDIOWATCHDOG_TRAYAPP_H
#define AUDIOWATCHDOG_TRAYAPP_H

#include <windows.h>

namespace aw {

// Blocks while the resident tray app runs. Returns the exit code on shutdown.
int RunTray(HINSTANCE hInstance);

} // namespace aw

#endif // AUDIOWATCHDOG_TRAYAPP_H