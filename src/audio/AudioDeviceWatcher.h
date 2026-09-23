#pragma once
// Watches the audio device tree via IMMNotificationClient and forwards every
// notification (device added / removed / state / default / property change)
// to a callback.
#ifndef AUDIOWATCHDOG_AUDIODEVICEWATCHER_H
#define AUDIOWATCHDOG_AUDIODEVICEWATCHER_H

#include "util/Win.h"
#include "util/ComPtr.h"

#include <functional>
#include <atomic>
#include <string>

namespace aw {

struct DeviceChange {
    enum class Kind { Added, Removed, StateChanged, DefaultChanged, PropertyChanged };
    Kind kind = Kind::PropertyChanged;
    std::wstring id;
    DWORD newState = 0; // StateChanged only
};

// The object is owned by its creator (usually a local variable), never by
// COM: Release() does not delete it. Stop() unregisters the callback before
// the object goes away, so the audio service holds no dangling reference.
class AudioDeviceWatcher final : public IMMNotificationClient {
public:
    AudioDeviceWatcher() = default;
    ~AudioDeviceWatcher();

    AudioDeviceWatcher(const AudioDeviceWatcher&) = delete;
    AudioDeviceWatcher& operator=(const AudioDeviceWatcher&) = delete;

    // Starts the background registration thread. `onChanged` is invoked from
    // an audio-service thread; it must be quick and must not block. Returns
    // false when the thread cannot be created.
    bool Start(std::function<void(const DeviceChange&)> onChanged);
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

    void Notify(DeviceChange::Kind kind, LPCWSTR id, DWORD state = 0);

    std::atomic<ULONG> refCount_{1};
    std::atomic<bool> delivering_{false};
    HANDLE stopEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    std::function<void(const DeviceChange&)> onChanged_;
};

} // namespace aw

#endif // AUDIOWATCHDOG_AUDIODEVICEWATCHER_H
