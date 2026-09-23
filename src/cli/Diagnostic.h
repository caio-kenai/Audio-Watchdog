#pragma once
// Diagnostic and listing helpers used by the CLI (devices/diagnose).
#ifndef AUDIOWATCHDOG_DIAGNOSTIC_H
#define AUDIOWATCHDOG_DIAGNOSTIC_H

#include <string>
#include <vector>

namespace aw {

// Prints an overview of the environment (OS, permissions, Core Audio status).
void PrintEnvironment();

// Enumerates every endpoint and prints a table with exclusive-mode state.
// Also runs the `scan` enforcement when `enforce` is true.
int RunDevicesCommand();

// Full self-check: OS, COM, enumeration, ability to read/write the exclusive
// settings on the default device, and (optionally) a real WASAPI probe.
int RunDiagnoseCommand(bool probeExclusive);

} // namespace aw

#endif // AUDIOWATCHDOG_DIAGNOSTIC_H