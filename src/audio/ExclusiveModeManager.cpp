#include "audio/ExclusiveModeManager.h"

#include "util/StrUtil.h"

namespace aw {

std::wstring FixResultName(FixResult r) {
    switch (r) {
        case FixResult::Unknown: return L"unknown";
        case FixResult::AlreadyOff: return L"off";
        case FixResult::Fixed: return L"fixed";
        case FixResult::WriteFailed: return L"write-failed";
        case FixResult::VerifyFailed: return L"verify-failed";
        case FixResult::Skipped: return L"skipped";
    }
    return L"unknown";
}

HRESULT ExclusiveModeManager::Inspect(EndpointInfo& info) {
    bool allow = false;
    bool priority = false;
    HRESULT hr = store_.Read(info.id, allow, priority);
    info.exclusiveReadHr = hr;
    info.exclusiveKnown = SUCCEEDED(hr);
    info.exclusiveAllowed = allow;
    info.exclusivePriority = priority;
    return hr;
}

FixResult ExclusiveModeManager::JudgeAndFix(EndpointInfo info, bool enforce) {
    HRESULT hr = Inspect(info);
    if (FAILED(hr)) {
        // Could not determine the state; the engine will retry on the next
        // cycle. Do not write blindly when we cannot read.
        return FixResult::Unknown;
    }
    if (!info.exclusiveAllowed) return FixResult::AlreadyOff;
    if (!enforce) return FixResult::Skipped;

    HRESULT w = store_.Write(info.id, false, false);
    if (FAILED(w)) return FixResult::WriteFailed;

    // Verify the write stuck.
    EndpointInfo verify = info;
    HRESULT r = Inspect(verify);
    if (FAILED(r)) return FixResult::VerifyFailed;
    if (verify.exclusiveAllowed) return FixResult::VerifyFailed;
    return FixResult::Fixed;
}

} // namespace aw