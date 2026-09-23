#pragma once
// Minimal RAII COM smart pointer. Avoids introducing an external dependency
// (e.g. WIL) while giving us safe exception-safe COM usage.
#ifndef AUDIOWATCHDOG_COMPTR_H
#define AUDIOWATCHDOG_COMPTR_H

#include <utility>
#include <type_traits>
#include <unknwn.h>

namespace aw {

template <typename T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    ~ComPtr() noexcept { reset(); }

    ComPtr(std::nullptr_t) noexcept {}
    ComPtr(T* raw, bool takeOwnership = true) noexcept : ptr_(raw) {
        if (raw && !takeOwnership) raw->AddRef();
    }
    ComPtr(const ComPtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            T* old = ptr_;
            ptr_ = other.ptr_;
            if (ptr_) ptr_->AddRef();
            if (old) old->Release();
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    void reset() noexcept {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    void attach(T* raw) noexcept {
        reset();
        ptr_ = raw;
    }
    T* detach() noexcept {
        T* r = ptr_;
        ptr_ = nullptr;
        return r;
    }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T** put() noexcept { return &ptr_; }
    [[nodiscard]] void** putVoid() noexcept { return reinterpret_cast<void**>(&ptr_); }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    template <typename U>
    HRESULT As(ComPtr<U>& out) const noexcept {
        out.reset();
        if (!ptr_) return E_POINTER;
        return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out.put()));
    }

private:
    T* ptr_ = nullptr;
};

} // namespace aw

#endif // AUDIOWATCHDOG_COMPTR_H