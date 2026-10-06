#pragma once

// GPLv3 source adaptation from WiiCompiled, commit
// 83463764b8acda394e058b0c689a10b8561fc380:
// https://github.com/patchzyy/Wiicompiled/blob/83463764b8acda394e058b0c689a10b8561fc380/runtime/include/guest_flat_memory.h
// Windows 4-GiB placeholder reservation and dual-view MEM1/MEM2 alias scheme.
// The native host can opt into this backing. A separately gated AOT path can
// consume it through GuestMemoryV1::flat_guest_read_base.

#include <cstddef>
#include <cstdint>

namespace galaxy::host::flat_guest_memory {

static_assert(sizeof(void*) == 8, "flat guest memory requires a 64-bit host");
inline constexpr std::uintptr_t kGuestBase = 0x0000100000000000ull;
inline constexpr std::uint64_t kGuestSpaceSize = 0x100000000ull;
inline constexpr std::uint32_t kMem1Size = 0x01800000u;
inline constexpr std::uint32_t kMem2Size = 0x04000000u;

// This process-lifetime allocation owns two shared physical sections and six
// guest views. Host code must use the host views; guest-address views may later
// carry page protections. initialize() must run before a module that emits
// kGuestBase + guest_address loads. It throws if exact placement is unavailable.
void initialize();
[[nodiscard]] bool is_initialized() noexcept;
[[nodiscard]] std::byte* mem1_host_view() noexcept;
[[nodiscard]] std::byte* mem2_host_view() noexcept;

// Returns the always-accessible host alias for a complete RAM span, or null.
// This can populate GuestMemoryV1 regions/fast_regions without copying bytes.
[[nodiscard]] std::byte* host_pointer(
    std::uint32_t guest_address,
    std::uint32_t size) noexcept;

// True only for a complete MEM1/MEM2 span in one of the six mapped aliases.
// Raw translated access outside these spans must fault; backing is never
// allocated on demand for unmapped addresses.
[[nodiscard]] bool is_mapped_ram_span(
    std::uint32_t guest_address,
    std::uint32_t size) noexcept;

// Only one live GuestAddressSpace may own the process-global section views.
// A second instance can retain independent vector-backed memory.
class OwnerLease {
public:
    OwnerLease() = default;
    ~OwnerLease();
    OwnerLease(const OwnerLease&) = delete;
    OwnerLease& operator=(const OwnerLease&) = delete;

    // False means another live instance owns the views. Fixed mapping errors
    // throw so an explicitly selected experiment fails before guest execution.
    [[nodiscard]] bool acquire();
    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    bool active_ = false;
};

}  // namespace galaxy::host::flat_guest_memory
