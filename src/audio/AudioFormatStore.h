#pragma once
// Real implementation of IAudioFormatStore.
//
//  - Current format: PKEY_AudioEngine_DeviceFormat from the endpoint property
//    store (public key, functiondiscoverykeys / mmdeviceapi).
//  - Supported formats: IKsFormatSupport on the endpoint's streaming
//    (Software_IO) KS pin, reached by walking IDeviceTopology from the endpoint
//    to the wave filter. This asks the driver directly, so it keeps working
//    while exclusive mode is blocked (IAudioClient::IsFormatSupported in
//    exclusive mode returns AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED then).
//  - Applying a format: IPolicyConfig::SetDeviceFormat, the COM interface the
//    Windows Sound control panel uses. Writing PKEY_AudioEngine_DeviceFormat
//    through IPropertyStore only changes the stored value; the audio engine
//    keeps its old mix format until restarted (verified experimentally), so
//    that path is deliberately not used.
#ifndef AUDIOWATCHDOG_AUDIOFORMATSTORE_H
#define AUDIOWATCHDOG_AUDIOFORMATSTORE_H

#include "audio/AudioTypes.h"

namespace aw {

class AudioFormatStore final : public IAudioFormatStore {
public:
    AudioFormatStore() = default;

    HRESULT GetDeviceFormat(const std::wstring& id, AudioFormat& out) override;
    HRESULT IsFormatSupported(const std::wstring& id, const AudioFormat& fmt,
                              bool& supported) override;
    HRESULT SetDeviceFormat(const std::wstring& id, const AudioFormat& fmt) override;
};

} // namespace aw

#endif // AUDIOWATCHDOG_AUDIOFORMATSTORE_H
