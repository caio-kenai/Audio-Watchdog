#include "tests/test_harness.h"
#include "tests/mocks.h"

#include "core/WatchdogEngine.h"

using namespace aw;

static Config TestConfig() {
    Config cfg;
    cfg.monitorPlayback = true;
    cfg.monitorCapture = true;
    cfg.checkIntervalSeconds = 1;
    cfg.enforce = true;
    return cfg;
}

TEST(WatchdogEngine_ScanNoEndpoints) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    WatchdogEngine engine(lister, store, formats, TestConfig());
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.endpointsScanned, 0);
    CHECK_EQ(rep.failed, 0);
}

TEST(WatchdogEngine_ScanFixesEnabledDevice) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;

    EndpointInfo render, capture;
    render.id = L"r1";
    render.flow = EndpointFlow::Render;
    render.deviceState = DEVICE_STATE_ACTIVE;
    store.allow[render.id] = true; // enabled -> must be fixed

    capture.id = L"c1";
    capture.flow = EndpointFlow::Capture;
    capture.deviceState = DEVICE_STATE_ACTIVE;
    store.allow[capture.id] = false; // already compliant

    lister.endpoints = { render, capture };

    WatchdogEngine engine(lister, store, formats, TestConfig());
    ScanReport rep = engine.ScanOnce();

    CHECK_EQ(rep.endpointsScanned, 2);
    CHECK_EQ(rep.fixed, 1);
    CHECK_EQ(rep.alreadyOff, 1);
    CHECK_EQ(rep.failed, 0);
    CHECK_EQ(store.writeCalls, 1);
    CHECK(!store.allow[L"r1"]);
}

TEST(WatchdogEngine_ScanRespectsCaptureFilter) {
    Config cfg = TestConfig();
    cfg.monitorCapture = false;

    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;

    EndpointInfo render, capture;
    render.id = L"r1";
    render.flow = EndpointFlow::Render;
    store.allow[render.id] = true;
    capture.id = L"c1";
    capture.flow = EndpointFlow::Capture;
    store.allow[capture.id] = true;

    lister.endpoints = { render, capture };

    WatchdogEngine engine(lister, store, formats, cfg);
    ScanReport rep = engine.ScanOnce();

    CHECK_EQ(rep.endpointsScanned, 1); // only render considered
    CHECK_EQ(rep.fixed, 1);
    CHECK(store.allow[L"c1"]); // capture untouched
}

TEST(WatchdogEngine_ScanSkippedWhenEnforcementOff) {
    Config cfg = TestConfig();
    cfg.enforce = false;

    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    EndpointInfo render;
    render.id = L"r1";
    render.flow = EndpointFlow::Render;
    store.allow[render.id] = true;
    lister.endpoints = { render };

    WatchdogEngine engine(lister, store, formats, cfg);
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.skipped, 1);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(WatchdogEngine_ScanReportsEnumerationFailure) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    // Force enumeration to fail by simulating a store/read issue? Instead use a
    // lister subclass that returns an error.
    struct FailLister : public IAudioDeviceLister {
        HRESULT Enumerate(std::vector<EndpointInfo>&) override { return E_FAIL; }
        HRESULT DefaultEndpointId(EndpointFlow, std::wstring&) override { return E_FAIL; }
    } failLister;

    WatchdogEngine engine(failLister, store, formats, TestConfig());
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.failed, 1);
}

static EndpointInfo ActiveRender(const wchar_t* id) {
    EndpointInfo e;
    e.id = id;
    e.name = id;
    e.flow = EndpointFlow::Render;
    e.deviceState = DEVICE_STATE_ACTIVE;
    return e;
}

TEST(WatchdogEngine_PausedDoesNotModify) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    lister.endpoints = { ActiveRender(L"r1") };
    store.allow[L"r1"] = true;

    WatchdogEngine engine(lister, store, formats, TestConfig());
    CHECK(engine.State() == WatchdogState::Running);
    engine.Pause();
    CHECK(engine.State() == WatchdogState::Paused);
    ScanReport rep = engine.ScanOnce();
    CHECK(rep.paused);
    CHECK_EQ(store.writeCalls, 0);
    CHECK(store.allow[L"r1"]);

    engine.Resume();
    CHECK(engine.State() == WatchdogState::Running);
    rep = engine.ScanOnce();
    CHECK_EQ(rep.fixed, 1);
    CHECK(!store.allow[L"r1"]);
}

TEST(WatchdogEngine_StateTransitions) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    Config cfg = TestConfig();
    cfg.checkIntervalSeconds = 3600;
    WatchdogEngine engine(lister, store, formats, cfg);
    CHECK(engine.Start());
    engine.Resume(); // no-op while running
    CHECK(engine.State() == WatchdogState::Running);
    engine.Pause();
    engine.Pause(); // idempotent
    CHECK(engine.State() == WatchdogState::Paused);
    engine.Stop();
    CHECK(engine.State() == WatchdogState::Stopped);
    engine.Stop(); // idempotent
}

TEST(WatchdogEngine_FormatStandardizationDisabledByDefault) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    lister.endpoints = { ActiveRender(L"r1") };
    formats.current[L"r1"] = awtest::Fmt(44100, 16, 16);
    formats.supported = { {48000, 24, 24, false} };

    WatchdogEngine engine(lister, store, formats, TestConfig());
    engine.ScanOnce();
    CHECK_EQ(formats.writeCalls, 0);
}

TEST(WatchdogEngine_FeaturesAreIndependent) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    lister.endpoints = { ActiveRender(L"r1") };
    store.allow[L"r1"] = true;
    formats.current[L"r1"] = awtest::Fmt(44100, 16, 16);
    formats.supported = { {48000, 24, 24, false} };

    // Format only: exclusive mode left alone.
    Config cfg = TestConfig();
    cfg.exclusiveModeProtection = false;
    cfg.formatStandardization = true;
    {
        WatchdogEngine engine(lister, store, formats, cfg);
        ScanReport rep = engine.ScanOnce();
        CHECK_EQ(rep.formatApplied, 1);
        CHECK_EQ(store.writeCalls, 0);
        CHECK(store.allow[L"r1"]);
    }
    // Both enabled.
    cfg.exclusiveModeProtection = true;
    {
        WatchdogEngine engine(lister, store, formats, cfg);
        ScanReport rep = engine.ScanOnce();
        CHECK_EQ(rep.fixed, 1);
        CHECK_EQ(rep.formatCompliant, 1);
    }
}

TEST(WatchdogEngine_FormatSkipsInactiveEndpoints) {
    awtest::MockLister lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    EndpointInfo unplugged = ActiveRender(L"r1");
    unplugged.deviceState = DEVICE_STATE_UNPLUGGED;
    lister.endpoints = { unplugged };
    formats.current[L"r1"] = awtest::Fmt(44100, 16, 16);
    formats.supported = { {48000, 24, 24, false} };
    Config cfg = TestConfig();
    cfg.formatStandardization = true;

    WatchdogEngine engine(lister, store, formats, cfg);
    engine.ScanOnce();
    CHECK_EQ(formats.writeCalls, 0);
}

TEST(WatchdogEngine_TriggeredScanBacksOffAfterFailure) {
    awtest::MockLister lister;
    awtest::StickyFailStore store; // driver keeps re-enabling exclusive mode
    awtest::MockFormatStore formats;
    lister.endpoints = { ActiveRender(L"r1") };

    WatchdogEngine engine(lister, store, formats, TestConfig());
    ScanReport first = engine.ScanOnce(ScanKind::Full);
    CHECK_EQ(first.failed, 1);
    // A notification right after must not hammer the device again...
    ScanReport triggered = engine.ScanOnce(ScanKind::Triggered);
    CHECK_EQ(triggered.skipped, 1);
    // ...but the next full (periodic) scan retries.
    ScanReport full = engine.ScanOnce(ScanKind::Full);
    CHECK_EQ(full.failed, 1);
}

TEST(WatchdogEngine_NoEndpointsIsNotAnError) {
    struct EmptyLister : public IAudioDeviceLister {
        HRESULT Enumerate(std::vector<EndpointInfo>&) override { return E_NOTFOUND; }
        HRESULT DefaultEndpointId(EndpointFlow, std::wstring&) override { return E_NOTFOUND; }
    } lister;
    awtest::MockStore store;
    awtest::MockFormatStore formats;
    WatchdogEngine engine(lister, store, formats, TestConfig());
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.failed, 0);
}
