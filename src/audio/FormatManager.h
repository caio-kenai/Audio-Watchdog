#pragma once
// Audio Format Standardization policy: checks an endpoint's default format
// against the configured target and applies it only when the driver reports
// that it supports it. Never falls back to a different format silently.
#ifndef AUDIOWATCHDOG_FORMATMANAGER_H
#define AUDIOWATCHDOG_FORMATMANAGER_H

#include "audio/AudioTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aw {

enum class FormatResult {
    Compliant,     // already at the target rate / bit depth
    Applied,       // changed to the target and verified
    Unsupported,   // the device does not support the target -> skipped
    Unknown,       // current or supported formats could not be determined
    WriteFailed,   // the change was rejected
    VerifyFailed,  // the change was accepted but did not stick
    Skipped,       // not compliant, but enforcement is off (report only)
};

std::wstring FormatResultName(FormatResult r);

struct FormatOutcome {
    FormatResult result = FormatResult::Unknown;
    AudioFormat before;   // format found on the device
    AudioFormat applied;  // variant written (Applied/VerifyFailed/WriteFailed)
    HRESULT hr = S_OK;    // failure detail for Unknown/WriteFailed
    std::wstring supported; // "44100 Hz / 16-bit, 48000 Hz / 24-bit" (Unsupported)
};

// "48000 Hz / 24-bit"
std::wstring DescribeFormat(const AudioFormat& f);

class FormatManager {
public:
    explicit FormatManager(IAudioFormatStore& store) : store_(store) {}

    // Checks one endpoint against sampleRate/bitDepth and, when `enforce` is
    // true and the format is supported, applies it.
    FormatOutcome JudgeAndApply(const std::wstring& id, std::uint32_t sampleRate,
                                std::uint16_t bitDepth, bool enforce);

    // Concrete sample layouts that realize a bit depth, in preference order
    // (24-bit may be packed 24/24 or 24-in-32 depending on the driver).
    static std::vector<AudioFormat> Candidates(std::uint32_t sampleRate, std::uint16_t bitDepth,
                                               std::uint16_t channels, std::uint32_t channelMask);

    // Lists the standard rates/depths the device supports (for the log).
    std::wstring DescribeSupported(const std::wstring& id, const AudioFormat& current);

private:
    IAudioFormatStore& store_;
};

} // namespace aw

#endif // AUDIOWATCHDOG_FORMATMANAGER_H
