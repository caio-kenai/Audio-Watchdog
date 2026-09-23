#include "tests/test_harness.h"
#include "tests/mocks.h"

#include "audio/ExclusiveModeManager.h"

using namespace aw;

TEST(ExclusiveModeManager_AlreadyOff) {
    awtest::MockStore store;
    store.allow[L"dev1"] = false;
    ExclusiveModeManager m(store);
    EndpointInfo info;
    info.id = L"dev1";
    CHECK_EQ(m.JudgeAndFix(info, /*enforce=*/true), FixResult::AlreadyOff);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(ExclusiveModeManager_Fixed) {
    awtest::MockStore store;
    store.allow[L"dev1"] = true;
    ExclusiveModeManager m(store);
    EndpointInfo info;
    info.id = L"dev1";
    CHECK_EQ(m.JudgeAndFix(info, /*enforce=*/true), FixResult::Fixed);
    CHECK_EQ(store.writeCalls, 1);
    CHECK(!store.allow[L"dev1"]); // now disabled
}

TEST(ExclusiveModeManager_SkippedWhenNotEnforcing) {
    awtest::MockStore store;
    store.allow[L"dev1"] = true;
    ExclusiveModeManager m(store);
    EndpointInfo info;
    info.id = L"dev1";
    CHECK_EQ(m.JudgeAndFix(info, /*enforce=*/false), FixResult::Skipped);
    CHECK_EQ(store.writeCalls, 0);
    CHECK(store.allow[L"dev1"]); // untouched
}

TEST(ExclusiveModeManager_UnknownOnReadFailure) {
    awtest::MockStore store;
    store.readHr = E_FAIL;
    ExclusiveModeManager m(store);
    EndpointInfo info;
    info.id = L"dev1";
    CHECK_EQ(m.JudgeAndFix(info, /*enforce=*/true), FixResult::Unknown);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(ExclusiveModeManager_WriteFailed) {
    awtest::WriteFailStore store;
    ExclusiveModeManager m(store);
    EndpointInfo info;
    info.id = L"dev1";
    CHECK_EQ(m.JudgeAndFix(info, /*enforce=*/true), FixResult::WriteFailed);
}

TEST(ExclusiveModeManager_VerifyFailed) {
    awtest::StickyFailStore store;
    ExclusiveModeManager m(store);
    EndpointInfo info;
    info.id = L"dev1";
    CHECK_EQ(m.JudgeAndFix(info, /*enforce=*/true), FixResult::VerifyFailed);
}