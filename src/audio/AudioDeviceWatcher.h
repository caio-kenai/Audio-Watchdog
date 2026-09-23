#pragma once
// Watches the audio device tree via IMMNotificationClient. Runs a dedicated
// STA thread so notifications are delivered on our own mailbox, and signals a
// user callback whenever anything changes.
#ifndef AUDIOWATCHDOG_AUDIODEVICEWATCHER_H
#define AUDIOWATCHDOG_AUDIODEVICEWATCHER_H

#include "util/Win.h"
#include "util/ComPtr.h"

#include <functional>
#include <atomic>

namespace aw {

class AudioDeviceWatcher final : public IMMNotificationClient {
public:
    AudioDeviceWatcher() = default;
    ~AudioDeviceWatcher();

    // Starts the background thread. `onChanged` is invoked (from the watcher
    // thread) whenever a notification arrives. Returns false on COM setup
    // failure.
    bool Start(std::function<void()> onChanged);
    void Stop();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;

private:
    static DWORD WINAPI ThreadProc(LPVOID arg);
    void ThreadMain();

    void Notify();

    std::atomic<ULONG> refCount_{1};
    std::atomic<bool> running_{false};
    HANDLE stopEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    std::function<void()> onChanged_;
    aw::ComPtr<IMMDeviceEnumerator> enumerator_;
};

} // namespace aw

#endif // AUDIOWATCHDOG_AUDIODEVICEWATCHER_H