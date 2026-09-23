#include "tests/test_harness.h"
#include "tests/mocks.h"

#include "audio/FormatManager.h"

using namespace aw;
using awtest::Fmt;

TEST(FormatManager_AlreadyCompliant) {
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(48000, 24, 32); // 24-in-32 counts as 24-bit
    FormatManager m(store);
    const FormatOutcome out = m.JudgeAndApply(L"d", 48000, 24, true);
    CHECK(out.result == FormatResult::Compliant);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(FormatManager_AppliesSupportedFormat) {
    // Requested 48000 Hz / 24-bit; device supports 44.1/16, 48/24, 96/24.
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(44100, 16, 16);
    store.supported = { {44100, 16, 16, false}, {48000, 24, 24, false}, {96000, 24, 24, false} };
    FormatManager m(store);
    const FormatOutcome out = m.JudgeAndApply(L"d", 48000, 24, true);
    CHECK(out.result == FormatResult::Applied);
    CHECK_EQ(store.writeCalls, 1);
    CHECK_EQ(store.current[L"d"].sampleRate, 48000u);
    CHECK_EQ(store.current[L"d"].validBits, static_cast<std::uint16_t>(24));
}

TEST(FormatManager_Uses24In32WhenPackedUnsupported) {
    // Realtek-style driver: 24-bit only in a 32-bit container.
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(44100, 16, 16);
    store.supported = { {48000, 16, 16, false}, {48000, 24, 32, false} };
    FormatManager m(store);
    const FormatOutcome out = m.JudgeAndApply(L"d", 48000, 24, true);
    CHECK(out.result == FormatResult::Applied);
    CHECK_EQ(store.lastWritten.containerBits, static_cast<std::uint16_t>(32));
    CHECK_EQ(store.lastWritten.validBits, static_cast<std::uint16_t>(24));
}

TEST(FormatManager_UnsupportedIsSkippedNotSubstituted) {
    // Requested 48000/24 on a device that only does 16-bit: never fall back.
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(44100, 16, 16);
    store.supported = { {44100, 16, 16, false}, {48000, 16, 16, false} };
    FormatManager m(store);
    const FormatOutcome out = m.JudgeAndApply(L"d", 48000, 24, true);
    CHECK(out.result == FormatResult::Unsupported);
    CHECK_EQ(store.writeCalls, 0);
    CHECK(out.supported.find(L"44100 Hz / 16-bit") != std::wstring::npos);
    CHECK(out.supported.find(L"48000 Hz / 16-bit") != std::wstring::npos);
}

TEST(FormatManager_UnknownWhenSupportCannotBeDetermined) {
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(44100, 16, 16);
    store.supportHr = E_NOINTERFACE;
    FormatManager m(store);
    const FormatOutcome out = m.JudgeAndApply(L"d", 48000, 24, true);
    CHECK(out.result == FormatResult::Unknown);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(FormatManager_UnknownWhenCurrentFormatUnreadable) {
    awtest::MockFormatStore store;
    store.readHr = E_FAIL;
    FormatManager m(store);
    CHECK(m.JudgeAndApply(L"d", 48000, 24, true).result == FormatResult::Unknown);
}

TEST(FormatManager_ReportOnlyWhenNotEnforcing) {
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(44100, 16, 16);
    store.supported = { {48000, 24, 24, false} };
    FormatManager m(store);
    CHECK(m.JudgeAndApply(L"d", 48000, 24, false).result == FormatResult::Skipped);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(FormatManager_WriteAndVerifyFailures) {
    awtest::MockFormatStore store;
    store.current[L"d"] = Fmt(44100, 16, 16);
    store.supported = { {48000, 24, 24, false} };
    store.writeHr = E_ACCESSDENIED;
    FormatManager m(store);
    CHECK(m.JudgeAndApply(L"d", 48000, 24, true).result == FormatResult::WriteFailed);

    store.writeHr = S_OK;
    store.writesStick = false;
    CHECK(m.JudgeAndApply(L"d", 48000, 24, true).result == FormatResult::VerifyFailed);
}

TEST(FormatManager_Candidates) {
    CHECK_EQ(FormatManager::Candidates(48000, 16, 2, 3).size(), static_cast<size_t>(1));
    const auto c24 = FormatManager::Candidates(48000, 24, 2, 3);
    CHECK_EQ(c24.size(), static_cast<size_t>(2));
    CHECK_EQ(c24[0].containerBits, static_cast<std::uint16_t>(24));
    CHECK_EQ(c24[1].containerBits, static_cast<std::uint16_t>(32));
    const auto c32 = FormatManager::Candidates(48000, 32, 2, 3);
    CHECK_EQ(c32.size(), static_cast<size_t>(2));
    CHECK(!c32[0].isFloat);
    CHECK(c32[1].isFloat);
}
