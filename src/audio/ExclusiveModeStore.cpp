#include "audio/ExclusiveModeStore.h"

#include "util/StrUtil.h"

#include <mmdeviceapi.h>
#include <propsys.h>

namespace aw {

namespace {

// Property key for "Allow applications to take exclusive control of this device".
constexpr PROPERTYKEY kPKEY_ExclusiveModeAllowed = {
    {0xB3F8FA53, 0x0004, 0x438E, {0x90, 0x03, 0x51, 0xA4, 0x6E, 0x13, 0x9B, 0xFC}}, 3};

// Property key for "Give exclusive mode applications priority".
constexpr PROPERTYKEY kPKEY_ExclusiveModePriority = {
    {0xB3F8FA53, 0x0004, 0x438E, {0x90, 0x03, 0x51, 0xA4, 0x6E, 0x13, 0x9B, 0xFC}}, 4};

HRESULT ReadUi4(IPropertyStore* store, const PROPERTYKEY& key, bool& out) {
    PROPVARIANT var;
    ::PropVariantInit(&var);
    HRESULT hr = store->GetValue(key, &var);
    if (SUCCEEDED(hr)) {
        if (var.vt == VT_UI4) {
            out = var.ulVal != 0;
        } else if (var.vt == VT_EMPTY) {
            out = false; // absent -> interpreted as blocked
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    ::PropVariantClear(&var);
    return hr;
}

} // namespace

HRESULT ExclusiveModeStore::OpenDeviceStore(const std::wstring& id, DWORD stgm,
                                            aw::ComPtr<IPropertyStore>& store) {
    if (id.empty()) return E_INVALIDARG;

    aw::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
    if (FAILED(hr)) return hr;

    aw::ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(id.c_str(), device.put());
    if (FAILED(hr)) return hr;

    return device->OpenPropertyStore(stgm, store.put());
}

HRESULT ExclusiveModeStore::Read(const std::wstring& id, bool& allow, bool& priority) {
    aw::ComPtr<IPropertyStore> store;
    HRESULT hr = OpenDeviceStore(id, STGM_READ, store);
    if (FAILED(hr)) return hr;

    allow = false;
    priority = false;

    HRESULT h1 = ReadUi4(store.get(), kPKEY_ExclusiveModeAllowed, allow);
    HRESULT h2 = ReadUi4(store.get(), kPKEY_ExclusiveModePriority, priority);

    // The allow key is the authoritative one; if reading it outright fails we
    // report failure so the engine can retry later.
    if (FAILED(h1)) return h1;
    return FAILED(h2) ? h2 : S_OK;
}

HRESULT ExclusiveModeStore::Write(const std::wstring& id, bool allow, bool priority) {
    aw::ComPtr<IPropertyStore> store;
    HRESULT hr = OpenDeviceStore(id, STGM_READWRITE, store);
    if (FAILED(hr)) return hr;

    PROPVARIANT var;
    ::PropVariantInit(&var);
    var.vt = VT_UI4;
    var.ulVal = allow ? 1UL : 0UL;
    hr = store->SetValue(kPKEY_ExclusiveModeAllowed, var);
    if (SUCCEEDED(hr)) {
        var.ulVal = priority ? 1UL : 0UL;
        hr = store->SetValue(kPKEY_ExclusiveModePriority, var);
    }
    ::PropVariantClear(&var);
    if (FAILED(hr)) return hr;

    return store->Commit();
}

HRESULT ExclusiveModeStore::VerifyWritable(const std::wstring& id) {
    // Confirm the endpoint exists and its exclusive setting is readable first.
    bool allow = false;
    bool priority = false;
    HRESULT hr = Read(id, allow, priority);
    if (FAILED(hr)) return hr;

    // Then confirm we can open the store for write and write the same value back.
    aw::ComPtr<IPropertyStore> store;
    hr = OpenDeviceStore(id, STGM_READWRITE, store);
    if (FAILED(hr)) return hr;

    PROPVARIANT var;
    ::PropVariantInit(&var);
    var.vt = VT_UI4;
    var.ulVal = allow ? 1UL : 0UL;
    hr = store->SetValue(kPKEY_ExclusiveModeAllowed, var);
    if (FAILED(hr)) {
        ::PropVariantClear(&var);
        return hr;
    }
    hr = store->Commit();
    ::PropVariantClear(&var);
    return hr;
}

} // namespace aw