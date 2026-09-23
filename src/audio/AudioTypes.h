#pragma once
// Shared audio types and interfaces used across Audio Watchdog.
#ifndef AUDIOWATCHDOG_AUDIOTYPES_H
#define AUDIOWATCHDOG_AUDIOTYPES_H

#include <string>
#include <vector>

#include "util/Win.h"

namespace aw {

enum class EndpointFlow {
    Unknown = 0,
    Render = 1,
    Capture = 2,
};

// One discovered audio endpoint (render or capture).
struct EndpointInfo {
    std::wstring id;         // stable device id, e.g. {0.0.0.00000000}.{...}
    std::wstring name;       // friendly name from the property store
    EndpointFlow flow = EndpointFlow::Unknown;
    DWORD deviceState = DEVICE_STATE_NOTPRESENT; // active / unplugged / disabled / notpresent
    bool isDefault = false;  // default console endpoint for its flow

    // Exclusive-mode state as reported by the property store.
    // `exclusiveKnown` is false when the store could not be read.
    bool exclusiveKnown = false;
    bool exclusiveAllowed = false;  // true => "allow exclusive control" is enabled
    bool exclusivePriority = false; // true => "give exclusive applications priority"
    HRESULT exclusiveReadHr = S_OK; // result of reading the store
};

// Abstraction over the audio endpoint database (enumeration + defaults).
class IAudioDeviceLister {
public:
    virtual ~IAudioDeviceLister() = default;

    // Enumerates active and unplugged render/capture endpoints.
    virtual HRESULT Enumerate(std::vector<EndpointInfo>& out) = 0;

    // Resolves the default (console) endpoint id for a flow.
    virtual HRESULT DefaultEndpointId(EndpointFlow flow, std::wstring& idOut) = 0;
};

// Abstraction over the per-endpoint exclusive-mode setting store.
class IExclusiveModeStore {
public:
    virtual ~IExclusiveModeStore() = default;

    // Reads the two exclusive-mode settings for an endpoint.
    //   allow    -> "allow applications to take exclusive control"
    //   priority -> "give exclusive mode applications priority"
    // Returns S_OK and sets the values, or an error HRESULT if unavailable.
    virtual HRESULT Read(const std::wstring& id, bool& allow, bool& priority) = 0;

    // Writes both settings persistently.
    virtual HRESULT Write(const std::wstring& id, bool allow, bool priority) = 0;
};

} // namespace aw

#endif // AUDIOWATCHDOG_AUDIOTYPES_H