#include "audio/AudioDeviceWatcher.h"

#include "logging/Logger.h"
#include "util/StrUtil.h"

namespace aw {

AudioDeviceWatcher::~AudioDeviceWatcher() {
    Stop();
}

HRESULT STDMETHODCALLTYPE AudioDeviceWatcher::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
        *ppvObject = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE AudioDeviceWatcher::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE AudioDeviceWatcher::Release() {
    // Lifetime is managed by the owner (see header), never deleted here.
    return --refCount_;
}

HRESULT AudioDeviceWatcher::OnDeviceStateChanged(LPCWSTR id, DWORD state) {
    Notify(DeviceChange::Kind::StateChanged, id, state);
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnDeviceAdded(LPCWSTR id) {
    Notify(DeviceChange::Kind::Added, id);
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnDeviceRemoved(LPCWSTR id) {
    Notify(DeviceChange::Kind::Removed, id);
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR id) {
    Notify(DeviceChange::Kind::DefaultChanged, id);
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY) {
    Notify(DeviceChange::Kind::PropertyChanged, id);
    return S_OK;
}

void AudioDeviceWatcher::Notify(DeviceChange::Kind kind, LPCWSTR id, DWORD state) {
    // Callbacks can arrive until UnregisterEndpointNotificationCallback
    // returns; `delivering_` is cleared before that call so late events are
    // dropped instead of reaching an owner that is shutting down.
    if (!delivering_ || !onChanged_) return;
    DeviceChange change;
    change.kind = kind;
    change.id = id ? id : L"";
    change.newState = state;
    try {
        onChanged_(change);
    } catch (...) {
        // Never let an exception cross the COM boundary into the audio service.
    }
}

DWORD WINAPI AudioDeviceWatcher::ThreadProc(LPVOID arg) {
    static_cast<AudioDeviceWatcher*>(arg)->ThreadMain();
    return 0;
}

void AudioDeviceWatcher::ThreadMain() {
    // IMMNotificationClient callbacks are delivered on audio-service threads,
    // not through this thread's apartment, so a plain MTA without a message
    // pump is correct and nothing needs to be polled.
    const HRESULT init = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        Logger::Instance().Error(L"AudioDeviceWatcher: CoInitializeEx failed: " + HrText(init));
        return;
    }

    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
        if (SUCCEEDED(hr)) {
            delivering_ = true;
            hr = enumerator->RegisterEndpointNotificationCallback(this);
            if (FAILED(hr)) delivering_ = false;
        }

        if (FAILED(hr)) {
            Logger::Instance().Error(L"AudioDeviceWatcher: cannot listen for device changes: " +
                                     HrText(hr) + L" (periodic checks still run).");
        } else {
            Logger::Instance().Info(L"AudioDeviceWatcher: listening for device changes");
            ::WaitForSingleObject(stopEvent_, INFINITE);
            delivering_ = false;
            enumerator->UnregisterEndpointNotificationCallback(this);
        }
    }

    if (SUCCEEDED(init)) ::CoUninitialize();
}

bool AudioDeviceWatcher::Start(std::function<void(const DeviceChange&)> onChanged) {
    Stop();

    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;

    onChanged_ = std::move(onChanged);
    thread_ = ::CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!thread_) {
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        return false;
    }
    return true;
}

void AudioDeviceWatcher::Stop() {
    if (stopEvent_) ::SetEvent(stopEvent_);
    if (thread_) {
        // Unregistering waits for in-flight callbacks, which are short.
        ::WaitForSingleObject(thread_, INFINITE);
        ::CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stopEvent_) {
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

} // namespace aw
