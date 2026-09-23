#pragma once
// CLI commands for Audio Watchdog.
#ifndef AUDIOWATCHDOG_CLI_H
#define AUDIOWATCHDOG_CLI_H

#include <string>

namespace aw {

// Returns 0 on success, non-zero on failure.
int RunCli(int argc, wchar_t** argv);

void PrintUsage();

} // namespace aw

#endif // AUDIOWATCHDOG_CLI_H