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
    WatchdogEngine engine(lister, store, TestConfig());
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.endpointsScanned, 0);
    CHECK_EQ(rep.failed, 0);
}

TEST(WatchdogEngine_ScanFixesEnabledDevice) {
    awtest::MockLister lister;
    awtest::MockStore store;

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

    WatchdogEngine engine(lister, store, TestConfig());
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

    EndpointInfo render, capture;
    render.id = L"r1";
    render.flow = EndpointFlow::Render;
    store.allow[render.id] = true;
    capture.id = L"c1";
    capture.flow = EndpointFlow::Capture;
    store.allow[capture.id] = true;

    lister.endpoints = { render, capture };

    WatchdogEngine engine(lister, store, cfg);
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
    EndpointInfo render;
    render.id = L"r1";
    render.flow = EndpointFlow::Render;
    store.allow[render.id] = true;
    lister.endpoints = { render };

    WatchdogEngine engine(lister, store, cfg);
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.skipped, 1);
    CHECK_EQ(store.writeCalls, 0);
}

TEST(WatchdogEngine_ScanReportsEnumerationFailure) {
    awtest::MockLister lister;
    awtest::MockStore store;
    // Force enumeration to fail by simulating a store/read issue? Instead use a
    // lister subclass that returns an error.
    struct FailLister : public IAudioDeviceLister {
        HRESULT Enumerate(std::vector<EndpointInfo>&) override { return E_FAIL; }
        HRESULT DefaultEndpointId(EndpointFlow, std::wstring&) override { return E_FAIL; }
    } failLister;

    WatchdogEngine engine(failLister, store, TestConfig());
    ScanReport rep = engine.ScanOnce();
    CHECK_EQ(rep.failed, 1);
}