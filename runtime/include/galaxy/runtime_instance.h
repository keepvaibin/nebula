#pragma once

#include <string_view>

namespace galaxy {

// Process-lifetime lease backed by a product-owned Windows named mutex. It
// rejects a second Nebula runtime without inspecting, focusing, closing,
// or terminating any unrelated process.
class RuntimeInstanceLease {
public:
    RuntimeInstanceLease() noexcept = default;
    ~RuntimeInstanceLease() noexcept;

    RuntimeInstanceLease(const RuntimeInstanceLease&) = delete;
    RuntimeInstanceLease& operator=(const RuntimeInstanceLease&) = delete;
    RuntimeInstanceLease(RuntimeInstanceLease&& other) noexcept;
    RuntimeInstanceLease& operator=(RuntimeInstanceLease&& other) noexcept;

    [[nodiscard]] bool acquire(
        std::wstring_view product_identity,
        bool& already_running) noexcept;
    [[nodiscard]] bool owns_lease() const noexcept;

private:
    void* mutex_ = nullptr;
};

}  // namespace galaxy
