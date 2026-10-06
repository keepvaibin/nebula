#include "galaxy/runtime_instance.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string>

namespace galaxy {

RuntimeInstanceLease::~RuntimeInstanceLease() noexcept {
    if (mutex_ != nullptr) {
        (void)ReleaseMutex(static_cast<HANDLE>(mutex_));
        (void)CloseHandle(static_cast<HANDLE>(mutex_));
    }
}

RuntimeInstanceLease::RuntimeInstanceLease(
    RuntimeInstanceLease&& other) noexcept
    : mutex_(other.mutex_) {
    other.mutex_ = nullptr;
}

RuntimeInstanceLease& RuntimeInstanceLease::operator=(
    RuntimeInstanceLease&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (mutex_ != nullptr) {
        (void)ReleaseMutex(static_cast<HANDLE>(mutex_));
        (void)CloseHandle(static_cast<HANDLE>(mutex_));
    }
    mutex_ = other.mutex_;
    other.mutex_ = nullptr;
    return *this;
}

bool RuntimeInstanceLease::acquire(
    std::wstring_view product_identity,
    bool& already_running) noexcept {
    already_running = false;
    if (mutex_ != nullptr || product_identity.empty() ||
        product_identity.find(L'\\') != std::wstring_view::npos) {
        return false;
    }

    try {
        const std::wstring name =
            std::wstring(L"Local\\") + std::wstring(product_identity);
        SetLastError(ERROR_SUCCESS);
        HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
        if (mutex == nullptr) {
            return false;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            already_running = true;
            (void)CloseHandle(mutex);
            return false;
        }
        mutex_ = mutex;
        return true;
    } catch (...) {
        return false;
    }
}

bool RuntimeInstanceLease::owns_lease() const noexcept {
    return mutex_ != nullptr;
}

}  // namespace galaxy
