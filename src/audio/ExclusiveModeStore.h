#pragma once
// Real implementation of IExclusiveModeStore backed by the per-endpoint
// Property Store exposed through the (public) MMDevice COM API.
//
// IMPORTANT — the property keys used here are the same ones the Windows Sound
// control panel (mmsys.cpl) toggles:
//   "Allow applications to take exclusive control of this device"
//   "Give exclusive mode applications priority"
// They live as PROPVARIANT VT_UI4 (0 = off, 1 = on). The *behavior* of these
// keys is observable and documented (writing 0 makes IMMDevice/IPropertyStore
// report the device blocked, verified with a real WASAPI exclusive probe), but
// the property keys themselves are not part of Microsoft's public SDK
// headers. They match the widely reproduced values used by third-party tools
// and confirmed against a real endpoint on Windows 10/11.
#ifndef AUDIOWATCHDOG_EXCLUSIVEMODESTORE_H
#define AUDIOWATCHDOG_EXCLUSIVEMODESTORE_H

#include "audio/AudioTypes.h"

#include "util/ComPtr.h"

namespace aw {

class ExclusiveModeStore final : public IExclusiveModeStore {
public:
    ExclusiveModeStore() = default;

    HRESULT Read(const std::wstring& id, bool& allow, bool& priority) override;
    HRESULT Write(const std::wstring& id, bool allow, bool priority) override;

    // Returns S_OK if the STGM_READWRITE property store can be opened and the
    // exclusive keys written back successfully (self-test for `diagnose`).
    HRESULT VerifyWritable(const std::wstring& id);

private:
    static HRESULT OpenDeviceStore(const std::wstring& id, DWORD stgm,
                                   aw::ComPtr<IPropertyStore>& store);
};

} // namespace aw

#endif // AUDIOWATCHDOG_EXCLUSIVEMODESTORE_H