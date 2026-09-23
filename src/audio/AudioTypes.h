#pragma once
// Shared audio types and interfaces used across Audio Watchdog.
#ifndef AUDIOWATCHDOG_AUDIOTYPES_H
#define AUDIOWATCHDOG_AUDIOTYPES_H

#include <cstdint>
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

// PCM/float sample format of an endpoint's shared-mode "Default Format".
struct AudioFormat {
    std::uint32_t sampleRate = 0;
    std::uint16_t validBits = 0;     // bit depth shown in the Sound panel
    std::uint16_t containerBits = 0; // storage size per sample (24 may live in 32)
    bool isFloat = false;
    std::uint16_t channels = 2;
    std::uint32_t channelMask = 0x3; // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
};

// Abstraction over the per-endpoint default (shared-mode) format.
class IAudioFormatStore {
public:
    virtual ~IAudioFormatStore() = default;

    // Reads the endpoint's current default format.
    virtual HRESULT GetDeviceFormat(const std::wstring& id, AudioFormat& out) = 0;

    // Asks the driver whether it can stream `fmt`. This is answered by the
    // device itself and does not depend on the exclusive-mode policy.
    // Returns a failure HRESULT when support cannot be determined.
    virtual HRESULT IsFormatSupported(const std::wstring& id, const AudioFormat& fmt,
                                      bool& supported) = 0;

    // Makes `fmt` the endpoint's default format (applied by the audio engine
    // immediately, exactly like the Sound control panel does).
    virtual HRESULT SetDeviceFormat(const std::wstring& id, const AudioFormat& fmt) = 0;
};

} // namespace aw

#endif // AUDIOWATCHDOG_AUDIOTYPES_H