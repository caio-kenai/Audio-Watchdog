#pragma once
// RAII wrapper for CoInitializeEx / CoUninitialize.
#ifndef AUDIOWATCHDOG_COMINITIALIZER_H
#define AUDIOWATCHDOG_COMINITIALIZER_H

#include "util/Win.h"

namespace aw {

class ComInitializer {
public:
    explicit ComInitializer(DWORD flags = COINIT_MULTITHREADED) noexcept : hr_(S_OK), active_(false) {
        hr_ = ::CoInitializeEx(nullptr, flags);
        active_ = SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE;
    }

    ~ComInitializer() noexcept {
        if (active_) ::CoUninitialize();
    }

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(hr_); }
    [[nodiscard]] HRESULT hr() const noexcept { return hr_; }

private:
    HRESULT hr_;
    bool active_;
};

} // namespace aw

#endif // AUDIOWATCHDOG_COMINITIALIZER_H