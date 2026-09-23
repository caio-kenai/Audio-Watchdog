#pragma once
// Test doubles for the audio interfaces.
#include "audio/AudioTypes.h"
#include "util/StrUtil.h"

#include <map>
#include <string>
#include <vector>

namespace awtest {

// In-memory exclusive-mode store.
class MockStore : public aw::IExclusiveModeStore {
public:
    // Current state: endpoint id -> allow value.
    std::map<std::wstring, bool> allow;
    std::map<std::wstring, bool> priority;
    // Injection points.
    HRESULT readHr = S_OK;
    HRESULT writeHr = S_OK;
    int writeCalls = 0;

    HRESULT Read(const std::wstring& id, bool& a, bool& p) override {
        if (FAILED(readHr)) return readHr;
        auto it = allow.find(id);
        a = it != allow.end() && it->second;
        auto ip = priority.find(id);
        p = ip != priority.end() && ip->second;
        return S_OK;
    }

    HRESULT Write(const std::wstring& id, bool a, bool p) override {
        ++writeCalls;
        if (FAILED(writeHr)) return writeHr;
        // Grant the write only if a "mutation allowed" behaviour wants it.
        allow[id] = a;
        priority[id] = p;
        return S_OK;
    }
};

// In-memory lister with a fixed set of endpoints.
class MockLister : public aw::IAudioDeviceLister {
public:
    std::vector<aw::EndpointInfo> endpoints;

    HRESULT Enumerate(std::vector<aw::EndpointInfo>& out) override {
        out = endpoints;
        return S_OK;
    }

    HRESULT DefaultEndpointId(aw::EndpointFlow flow, std::wstring& idOut) override {
        for (const auto& e : endpoints) {
            if (e.flow == flow && e.isDefault) {
                idOut = e.id;
                return S_OK;
            }
        }
        return E_NOTFOUND;
    }
};

// Store that simulates a write that never sticks (verify fails).
class StickyFailStore : public aw::IExclusiveModeStore {
public:
    bool allow = true;
    HRESULT Read(const std::wstring&, bool& a, bool& p) override {
        a = allow;
        p = allow;
        return S_OK;
    }
    HRESULT Write(const std::wstring&, bool, bool) override {
        allow = true; // stubborn driver
        return S_OK;
    }
};

// Store whose writes fail outright.
class WriteFailStore : public aw::IExclusiveModeStore {
public:
    HRESULT Read(const std::wstring&, bool& a, bool&) override {
        a = true;
        return S_OK;
    }
    HRESULT Write(const std::wstring&, bool, bool) override { return E_ACCESSDENIED; }
};

} // namespace awtest