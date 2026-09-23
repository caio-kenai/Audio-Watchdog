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

// In-memory default-format store.
class MockFormatStore : public aw::IAudioFormatStore {
public:
    std::map<std::wstring, aw::AudioFormat> current;
    // Supported layouts: (rate, validBits, containerBits, float).
    struct Layout { std::uint32_t rate; std::uint16_t valid; std::uint16_t container; bool isFloat; };
    std::vector<Layout> supported;
    HRESULT readHr = S_OK;
    HRESULT supportHr = S_OK;
    HRESULT writeHr = S_OK;
    bool writesStick = true;
    int writeCalls = 0;
    aw::AudioFormat lastWritten;

    HRESULT GetDeviceFormat(const std::wstring& id, aw::AudioFormat& out) override {
        if (FAILED(readHr)) return readHr;
        auto it = current.find(id);
        if (it == current.end()) return E_NOTFOUND;
        out = it->second;
        return S_OK;
    }
    HRESULT IsFormatSupported(const std::wstring&, const aw::AudioFormat& f, bool& ok) override {
        ok = false;
        if (FAILED(supportHr)) return supportHr;
        for (const auto& l : supported) {
            if (l.rate == f.sampleRate && l.valid == f.validBits && l.container == f.containerBits &&
                l.isFloat == f.isFloat) {
                ok = true;
            }
        }
        return S_OK;
    }
    HRESULT SetDeviceFormat(const std::wstring& id, const aw::AudioFormat& f) override {
        ++writeCalls;
        lastWritten = f;
        if (FAILED(writeHr)) return writeHr;
        if (writesStick) current[id] = f;
        return S_OK;
    }
};

inline aw::AudioFormat Fmt(std::uint32_t rate, std::uint16_t valid, std::uint16_t container) {
    aw::AudioFormat f;
    f.sampleRate = rate;
    f.validBits = valid;
    f.containerBits = container;
    return f;
}

} // namespace awtest