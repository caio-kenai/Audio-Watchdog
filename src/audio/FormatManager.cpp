#include "audio/FormatManager.h"

#include "util/StrUtil.h"

namespace aw {

namespace {

bool Matches(const AudioFormat& f, std::uint32_t sampleRate, std::uint16_t bitDepth) {
    return f.sampleRate == sampleRate && f.validBits == bitDepth;
}

} // namespace

std::wstring FormatResultName(FormatResult r) {
    switch (r) {
        case FormatResult::Compliant: return L"compliant";
        case FormatResult::Applied: return L"applied";
        case FormatResult::Unsupported: return L"unsupported";
        case FormatResult::Unknown: return L"unknown";
        case FormatResult::WriteFailed: return L"write-failed";
        case FormatResult::VerifyFailed: return L"verify-failed";
        case FormatResult::Skipped: return L"skipped";
    }
    return L"unknown";
}

std::wstring DescribeFormat(const AudioFormat& f) {
    return FormatW(L"%u Hz / %u-bit", f.sampleRate, static_cast<unsigned>(f.validBits));
}

std::vector<AudioFormat> FormatManager::Candidates(std::uint32_t sampleRate, std::uint16_t bitDepth,
                                                   std::uint16_t channels, std::uint32_t channelMask) {
    AudioFormat base;
    base.sampleRate = sampleRate;
    base.validBits = bitDepth;
    base.channels = channels == 0 ? 2 : channels;
    base.channelMask = channelMask;

    std::vector<AudioFormat> out;
    auto add = [&](std::uint16_t container, bool isFloat) {
        AudioFormat f = base;
        f.containerBits = container;
        f.isFloat = isFloat;
        out.push_back(f);
    };
    switch (bitDepth) {
        case 16: add(16, false); break;
        case 24: add(24, false); add(32, false); break;
        case 32: add(32, false); add(32, true); break;
        default: break;
    }
    return out;
}

std::wstring FormatManager::DescribeSupported(const std::wstring& id, const AudioFormat& current) {
    static constexpr std::uint32_t kRates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
    static constexpr std::uint16_t kDepths[] = { 16, 24, 32 };
    std::wstring list;
    for (std::uint32_t rate : kRates) {
        for (std::uint16_t depth : kDepths) {
            for (const AudioFormat& f : Candidates(rate, depth, current.channels, current.channelMask)) {
                bool ok = false;
                if (FAILED(store_.IsFormatSupported(id, f, ok))) return L"(could not be determined)";
                if (ok) {
                    if (!list.empty()) list += L", ";
                    list += DescribeFormat(f);
                    break;
                }
            }
        }
    }
    return list.empty() ? L"(none of the standard PCM formats)" : list;
}

FormatOutcome FormatManager::JudgeAndApply(const std::wstring& id, std::uint32_t sampleRate,
                                           std::uint16_t bitDepth, bool enforce) {
    FormatOutcome out;
    out.hr = store_.GetDeviceFormat(id, out.before);
    if (FAILED(out.hr)) {
        out.result = FormatResult::Unknown;
        return out;
    }
    if (Matches(out.before, sampleRate, bitDepth)) {
        out.result = FormatResult::Compliant;
        return out;
    }

    // Check the requested format with the driver before touching anything.
    const AudioFormat* chosen = nullptr;
    const std::vector<AudioFormat> candidates =
        Candidates(sampleRate, bitDepth, out.before.channels, out.before.channelMask);
    for (const AudioFormat& f : candidates) {
        bool ok = false;
        const HRESULT hr = store_.IsFormatSupported(id, f, ok);
        if (FAILED(hr)) {
            out.hr = hr;
            out.result = FormatResult::Unknown;
            return out;
        }
        if (ok) {
            chosen = &f;
            break;
        }
    }
    if (chosen == nullptr) {
        out.result = FormatResult::Unsupported;
        out.supported = DescribeSupported(id, out.before);
        return out;
    }

    out.applied = *chosen;
    if (!enforce) {
        out.result = FormatResult::Skipped;
        return out;
    }

    out.hr = store_.SetDeviceFormat(id, *chosen);
    if (FAILED(out.hr)) {
        out.result = FormatResult::WriteFailed;
        return out;
    }

    AudioFormat after;
    if (FAILED(store_.GetDeviceFormat(id, after)) || !Matches(after, sampleRate, bitDepth)) {
        out.result = FormatResult::VerifyFailed;
        return out;
    }
    out.result = FormatResult::Applied;
    return out;
}

} // namespace aw
