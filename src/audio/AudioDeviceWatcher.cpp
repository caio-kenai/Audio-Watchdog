#include "audio/AudioDeviceWatcher.h"

#include "logging/Logger.h"
#include "util/StrUtil.h"

namespace aw {

namespace {

void NotifyOnce(std::function<void()> fn) {
    if (fn) fn();
}

} // namespace

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
    const ULONG n = --refCount_;
    if (n == 0) delete this;
    return n;
}

HRESULT AudioDeviceWatcher::OnDeviceStateChanged(LPCWSTR, DWORD) {
    Notify();
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnDeviceAdded(LPCWSTR) {
    Notify();
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnDeviceRemoved(LPCWSTR) {
    Notify();
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) {
    Notify();
    return S_OK;
}

HRESULT AudioDeviceWatcher::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) {
    Notify();
    return S_OK;
}

void AudioDeviceWatcher::Notify() {
    NotifyOnce(onChanged_);
}

DWORD WINAPI AudioDeviceWatcher::ThreadProc(LPVOID arg) {
    auto* self = static_cast<AudioDeviceWatcher*>(arg);
    self->ThreadMain();
    return 0;
}

void AudioDeviceWatcher::ThreadMain() {
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        running_ = false;
        ::SetEvent(stopEvent_);
        Logger::Instance().Error(L"AudioDeviceWatcher: CoInitializeEx failed: " + HrText(hr));
        return;
    }
    const bool needCoUninit = SUCCEEDED(hr);

    hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                            __uuidof(IMMDeviceEnumerator), enumerator_.putVoid());
    if (FAILED(hr)) {
        Logger::Instance().Error(L"AudioDeviceWatcher: cannot create MMDeviceEnumerator: " + HrText(hr));
        if (needCoUninit) ::CoUninitialize();
        running_ = false;
        ::SetEvent(stopEvent_);
        return;
    }

    hr = enumerator_->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr)) {
        Logger::Instance().Error(L"AudioDeviceWatcher: RegisterEndpointNotificationCallback failed: " +
                                 HrText(hr));
        enumerator_.reset();
        if (needCoUninit) ::CoUninitialize();
        running_ = false;
        ::SetEvent(stopEvent_);
        return;
    }

    Logger::Instance().Info(L"AudioDeviceWatcher: listening for device changes");

    // Pump messages while waiting for the stop signal. Notifications are
    // delivered on this thread by the COM apartment.
    while (running_) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        const DWORD wait = ::MsgWaitForMultipleObjects(1, &stopEvent_, FALSE, 500, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) break;
    }

    if (enumerator_) {
        enumerator_->UnregisterEndpointNotificationCallback(this);
        enumerator_.reset();
    }
    if (needCoUninit) ::CoUninitialize();
}

bool AudioDeviceWatcher::Start(std::function<void()> onChanged) {
    Stop();

    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;

    onChanged_ = std::move(onChanged);
    running_ = true;

    thread_ = ::CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!thread_) {
        running_ = false;
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        return false;
    }
    return true;
}

void AudioDeviceWatcher::Stop() {
    running_ = false;
    if (stopEvent_) ::SetEvent(stopEvent_);
    if (thread_) {
        if (::WaitForSingleObject(thread_, 3000) == WAIT_TIMEOUT) {
            ::TerminateThread(thread_, 0);
        }
        ::CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stopEvent_) {
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

} // namespace aw