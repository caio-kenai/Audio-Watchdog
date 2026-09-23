#pragma once
// Real implementation of IAudioDeviceLister using the Windows Core Audio
// MMDevice API (IMMDeviceEnumerator / IMMDevice / IPropertyStore).
#ifndef AUDIOWATCHDOG_COREAUDIODEVICEMANAGER_H
#define AUDIOWATCHDOG_COREAUDIODEVICEMANAGER_H

#include "audio/AudioTypes.h"

namespace aw {

class CoreAudioDeviceLister final : public IAudioDeviceLister {
public:
    CoreAudioDeviceLister() = default;

    HRESULT Enumerate(std::vector<EndpointInfo>& out) override;
    HRESULT DefaultEndpointId(EndpointFlow flow, std::wstring& idOut) override;

private:
    // Creates an IMMDeviceEnumerator (caller keeps an STA/MTA COM initialized).
    static HRESULT CreateEnumerator(void** ppEnumerator);

    static HRESULT FillEndpointInfo(void* pEnumerator, void* pDevice, EndpointFlow flow,
                                    std::vector<EndpointInfo>& out);
};

} // namespace aw

#endif // AUDIOWATCHDOG_COREAUDIODEVICEMANAGER_H