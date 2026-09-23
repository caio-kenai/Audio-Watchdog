#pragma once
// Centralizes the exclusive-mode policy: inspect an endpoint's setting and
// force it off when enforcement is enabled.
#ifndef AUDIOWATCHDOG_EXCLUSIVEMODEMANAGER_H
#define AUDIOWATCHDOG_EXCLUSIVEMODEMANAGER_H

#include "audio/AudioTypes.h"

namespace aw {

// Outcome of inspecting + remediating a single endpoint.
enum class FixResult {
    Unknown,       // nothing done; the store could not be read (retry later)
    AlreadyOff,    // exclusive mode already disabled
    Fixed,         // was enabled; successfully set to off
    WriteFailed,   // was enabled; write did not succeed
    VerifyFailed,  // write succeeded but read-back did not confirm 0
    Skipped,       // enforcement disabled by configuration
};

std::wstring FixResultName(FixResult r);

class ExclusiveModeManager {
public:
    explicit ExclusiveModeManager(IExclusiveModeStore& store) : store_(store) {}

    // Resolves the exclusive state for an endpoint (fills exclusive* fields).
    HRESULT Inspect(EndpointInfo& info);

    // Inspects an endpoint and, when `enforce` is true, disables exclusive
    // mode if it is currently enabled. Always performs a read-back to verify.
    FixResult JudgeAndFix(EndpointInfo info, bool enforce);

private:
    IExclusiveModeStore& store_;
};

} // namespace aw

#endif // AUDIOWATCHDOG_EXCLUSIVEMODEMANAGER_H