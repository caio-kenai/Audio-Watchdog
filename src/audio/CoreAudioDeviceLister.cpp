#include "audio/CoreAudioDeviceLister.h"

#include "util/ComPtr.h"
#include "util/StrUtil.h"

#include <mmdeviceapi.h>
#include <functiondiscoverykeys.h>
#include <propsys.h>

namespace aw {

namespace {

EDataFlow FlowToEDataFlow(EndpointFlow flow) {
    switch (flow) {
        case EndpointFlow::Render: return eRender;
        case EndpointFlow::Capture: return eCapture;
        default: return eRender;
    }
}

HRESULT GetDeviceId(void* pDevice, std::wstring& idOut) {
    auto* dev = static_cast<IMMDevice*>(pDevice);
    LPWSTR szId = nullptr;
    HRESULT hr = dev->GetId(&szId);
    if (SUCCEEDED(hr)) {
        idOut = szId ? szId : L"";
        ::CoTaskMemFree(szId);
    }
    return hr;
}

HRESULT GetFriendlyName(void* pDevice, std::wstring& nameOut) {
    auto* dev = static_cast<IMMDevice*>(pDevice);
    aw::ComPtr<IPropertyStore> store;
    HRESULT hr = dev->OpenPropertyStore(STGM_READ, store.put());
    if (FAILED(hr)) {
        nameOut = L"(unavailable)";
        return hr;
    }
    PROPVARIANT var;
    ::PropVariantInit(&var);
    hr = store->GetValue(PKEY_Device_FriendlyName, &var);
    if (SUCCEEDED(hr) && var.vt == VT_LPWSTR && var.pwszVal) {
        nameOut = var.pwszVal;
    } else {
        nameOut = L"(unnamed)";
        hr = hr == S_OK ? E_FAIL : hr;
    }
    ::PropVariantClear(&var);
    return hr;
}

} // namespace

HRESULT CoreAudioDeviceLister::CreateEnumerator(void** ppEnumerator) {
    if (!ppEnumerator) return E_POINTER;
    *ppEnumerator = nullptr;
    return ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), ppEnumerator);
}

HRESULT CoreAudioDeviceLister::FillEndpointInfo(void* pEnumerator, void* pDevice, EndpointFlow flow,
                                                std::vector<EndpointInfo>& out) {
    auto* enumerator = static_cast<IMMDeviceEnumerator*>(pEnumerator);
    auto* dev = static_cast<IMMDevice*>(pDevice);

    EndpointInfo info;
    info.flow = flow;
    HRESULT hr = GetDeviceId(dev, info.id);
    if (FAILED(hr)) return hr;

    DWORD state = 0;
    hr = dev->GetState(&state);
    info.deviceState = SUCCEEDED(hr) ? state : DEVICE_STATE_NOTPRESENT;

    GetFriendlyName(dev, info.name);

    // Determine whether this is the default console endpoint.
    aw::ComPtr<IMMDevice> def;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(FlowToEDataFlow(flow), eConsole, def.put()))) {
        LPWSTR defId = nullptr;
        if (SUCCEEDED(def->GetId(&defId))) {
            info.isDefault = (defId && info.id == std::wstring(defId));
            ::CoTaskMemFree(defId);
        }
    }

    out.push_back(std::move(info));
    return S_OK;
}

HRESULT CoreAudioDeviceLister::Enumerate(std::vector<EndpointInfo>& out) {
    aw::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateEnumerator(enumerator.putVoid());
    if (FAILED(hr)) return hr;

    for (EndpointFlow flow : { EndpointFlow::Render, EndpointFlow::Capture }) {
        aw::ComPtr<IMMDeviceCollection> collection;
        hr = enumerator->EnumAudioEndpoints(FlowToEDataFlow(flow),
                                            DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED,
                                            collection.put());
        if (FAILED(hr)) continue;

        UINT count = 0;
        hr = collection->GetCount(&count);
        if (FAILED(hr)) continue;

        for (UINT i = 0; i < count; ++i) {
            aw::ComPtr<IMMDevice> device;
            if (SUCCEEDED(collection->Item(i, device.put()))) {
                FillEndpointInfo(enumerator.get(), device.get(), flow, out);
            }
        }
    }
    return out.empty() ? E_NOTFOUND : S_OK;
}

HRESULT CoreAudioDeviceLister::DefaultEndpointId(EndpointFlow flow, std::wstring& idOut) {
    aw::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateEnumerator(enumerator.putVoid());
    if (FAILED(hr)) return hr;

    aw::ComPtr<IMMDevice> def;
    hr = enumerator->GetDefaultAudioEndpoint(FlowToEDataFlow(flow), eConsole, def.put());
    if (SUCCEEDED(hr)) {
        LPWSTR szId = nullptr;
        hr = def->GetId(&szId);
        if (SUCCEEDED(hr)) {
            idOut = szId ? szId : L"";
            ::CoTaskMemFree(szId);
        }
    }
    return hr;
}

} // namespace aw