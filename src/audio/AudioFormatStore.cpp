#include "audio/AudioFormatStore.h"

#include "util/ComPtr.h"

#include <devicetopology.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <vector>

namespace aw {

namespace {

// PKEY_AudioEngine_DeviceFormat {f19f064d-082c-4e27-bc73-6882a1bb8e4c},0
constexpr PROPERTYKEY kPKEY_DeviceFormat = {
    {0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};

// IPolicyConfig (Windows 7 .. 11). Only the methods up to SetDeviceFormat are
// declared; the vtable order is what matters.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
};

// CLSID_CPolicyConfigClient {870af99c-171d-4f9e-af0d-e63df40c2bc9}
constexpr CLSID kCLSID_PolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

struct KsWaveFormat {
    KSDATAFORMAT header;
    WAVEFORMATEXTENSIBLE wave;
};

WAVEFORMATEXTENSIBLE ToWave(const AudioFormat& f) {
    WAVEFORMATEXTENSIBLE w{};
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels = f.channels;
    w.Format.nSamplesPerSec = f.sampleRate;
    w.Format.wBitsPerSample = f.containerBits;
    w.Format.nBlockAlign = static_cast<WORD>(f.channels * f.containerBits / 8);
    w.Format.nAvgBytesPerSec = f.sampleRate * w.Format.nBlockAlign;
    w.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    w.Samples.wValidBitsPerSample = f.validBits;
    w.dwChannelMask = f.channelMask;
    w.SubFormat = f.isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    return w;
}

HRESULT OpenDevice(const std::wstring& id, ComPtr<IMMDevice>& device) {
    if (id.empty()) return E_INVALIDARG;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
    if (FAILED(hr)) return hr;
    return enumerator->GetDevice(id.c_str(), device.put());
}

// Walks from the endpoint through the KS filter graph (topology filter ->
// wave filter) looking for the Software_IO connector, which exposes
// IKsFormatSupport for the pin the audio engine streams to.
HRESULT FindFormatSupport(IMMDevice* device, ComPtr<IKsFormatSupport>& out) {
    ComPtr<IDeviceTopology> endpointTopology;
    HRESULT hr = device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr,
                                  endpointTopology.putVoid());
    if (FAILED(hr)) return hr;

    ComPtr<IConnector> endpointConnector;
    hr = endpointTopology->GetConnector(0, endpointConnector.put());
    if (FAILED(hr)) return hr;
    ComPtr<IConnector> adapterConnector;
    hr = endpointConnector->GetConnectedTo(adapterConnector.put());
    if (FAILED(hr)) return hr;
    ComPtr<IPart> adapterPart;
    hr = adapterConnector.As(adapterPart);
    if (FAILED(hr)) return hr;

    std::vector<ComPtr<IDeviceTopology>> queue;
    std::vector<std::wstring> visited;
    {
        ComPtr<IDeviceTopology> first;
        hr = adapterPart->GetTopologyObject(first.put());
        if (FAILED(hr)) return hr;
        queue.push_back(std::move(first));
    }

    // Real graphs are 1-3 filters deep; the bound guards against cycles in
    // odd drivers.
    for (size_t head = 0; head < queue.size() && head < 8; ++head) {
        IDeviceTopology* topology = queue[head].get();
        LPWSTR rawId = nullptr;
        if (FAILED(topology->GetDeviceId(&rawId)) || rawId == nullptr) continue;
        const std::wstring filterId = rawId;
        ::CoTaskMemFree(rawId);
        bool seen = false;
        for (const auto& v : visited) seen = seen || v == filterId;
        if (seen) continue;
        visited.push_back(filterId);

        UINT count = 0;
        if (FAILED(topology->GetConnectorCount(&count))) continue;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IConnector> connector;
            if (FAILED(topology->GetConnector(i, connector.put()))) continue;
            ConnectorType type = Unknown_Connector;
            if (FAILED(connector->GetType(&type))) continue;
            if (type == Software_IO) {
                ComPtr<IPart> part;
                if (SUCCEEDED(connector.As(part)) &&
                    SUCCEEDED(part->Activate(CLSCTX_ALL, __uuidof(IKsFormatSupport),
                                             reinterpret_cast<void**>(out.put())))) {
                    return S_OK;
                }
                continue;
            }
            ComPtr<IConnector> other;
            if (FAILED(connector->GetConnectedTo(other.put()))) continue;
            ComPtr<IPart> otherPart;
            if (FAILED(other.As(otherPart))) continue;
            ComPtr<IDeviceTopology> next;
            if (SUCCEEDED(otherPart->GetTopologyObject(next.put()))) {
                queue.push_back(std::move(next));
            }
        }
    }
    return E_NOINTERFACE;
}

} // namespace

HRESULT AudioFormatStore::GetDeviceFormat(const std::wstring& id, AudioFormat& out) {
    ComPtr<IMMDevice> device;
    HRESULT hr = OpenDevice(id, device);
    if (FAILED(hr)) return hr;
    ComPtr<IPropertyStore> store;
    hr = device->OpenPropertyStore(STGM_READ, store.put());
    if (FAILED(hr)) return hr;

    PROPVARIANT var;
    ::PropVariantInit(&var);
    hr = store->GetValue(kPKEY_DeviceFormat, &var);
    if (SUCCEEDED(hr)) {
        if (var.vt == VT_BLOB && var.blob.pBlobData != nullptr &&
            var.blob.cbSize >= sizeof(WAVEFORMATEX)) {
            const auto* wf = reinterpret_cast<const WAVEFORMATEX*>(var.blob.pBlobData);
            out = AudioFormat{};
            out.sampleRate = wf->nSamplesPerSec;
            out.containerBits = wf->wBitsPerSample;
            out.validBits = wf->wBitsPerSample;
            out.channels = wf->nChannels;
            out.channelMask = wf->nChannels == 1 ? SPEAKER_FRONT_CENTER
                                                 : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
            out.isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                var.blob.cbSize >= sizeof(WAVEFORMATEXTENSIBLE)) {
                const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
                if (ext->Samples.wValidBitsPerSample != 0) {
                    out.validBits = ext->Samples.wValidBitsPerSample;
                }
                out.channelMask = ext->dwChannelMask;
                out.isFloat = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            }
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    ::PropVariantClear(&var);
    return hr;
}

HRESULT AudioFormatStore::IsFormatSupported(const std::wstring& id, const AudioFormat& fmt,
                                            bool& supported) {
    supported = false;
    ComPtr<IMMDevice> device;
    HRESULT hr = OpenDevice(id, device);
    if (FAILED(hr)) return hr;
    ComPtr<IKsFormatSupport> formatSupport;
    hr = FindFormatSupport(device.get(), formatSupport);
    if (FAILED(hr)) return hr;

    KsWaveFormat ks{};
    ks.wave = ToWave(fmt);
    ks.header.FormatSize = sizeof(KsWaveFormat);
    ks.header.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    ks.header.SubFormat = ks.wave.SubFormat;
    ks.header.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    ks.header.SampleSize = ks.wave.Format.nBlockAlign;

    BOOL ok = FALSE;
    hr = formatSupport->IsFormatSupported(&ks.header, sizeof(ks), &ok);
    if (SUCCEEDED(hr)) supported = ok != FALSE;
    return hr;
}

HRESULT AudioFormatStore::SetDeviceFormat(const std::wstring& id, const AudioFormat& fmt) {
    if (id.empty()) return E_INVALIDARG;
    ComPtr<IPolicyConfig> policy;
    HRESULT hr = ::CoCreateInstance(kCLSID_PolicyConfigClient, nullptr, CLSCTX_ALL,
                                    __uuidof(IPolicyConfig), policy.putVoid());
    if (FAILED(hr)) return hr;

    WAVEFORMATEXTENSIBLE device = ToWave(fmt);
    // The shared-mode mix format is always 32-bit float at the device rate.
    AudioFormat mixFmt = fmt;
    mixFmt.containerBits = 32;
    mixFmt.validBits = 32;
    mixFmt.isFloat = true;
    WAVEFORMATEXTENSIBLE mix = ToWave(mixFmt);
    return policy->SetDeviceFormat(id.c_str(), reinterpret_cast<WAVEFORMATEX*>(&device),
                                   reinterpret_cast<WAVEFORMATEX*>(&mix));
}

} // namespace aw
