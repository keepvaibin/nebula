#pragma once

// GPLv3 implementation adaptation from WiiCompiled, commit
// 83463764b8acda394e058b0c689a10b8561fc380:
// https://github.com/patchzyy/Wiicompiled/blob/83463764b8acda394e058b0c689a10b8561fc380/runtime/include/memory_access.h
// Specifically MemoryInline::FlatLoad and FlatRead8/16/32/64. Galaxy adds a
// complete RAM-span and backing-identity guard, with its checked read helper
// as the fallback. This test-facing helper is distinct from the opt-in AOT
// path in native_api.h, which uses a per-instance flat-read capability.

#include "galaxy/flat_guest_memory.h"
#include "galaxy/native_api.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace galaxy::host::flat_guest_read {

// Comparing host aliases prevents this optional fast path from reading a
// different GuestMemoryV1 instance's RAM. It also rejects unmapped/partial
// spans before any access to the reserved fixed guest view.
GALAXY_ALWAYS_INLINE bool eligible(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) noexcept {
    const std::byte* mapped = flat_guest_memory::host_pointer(address, size);
    return mapped != nullptr &&
           resolve_guest_fast(memory, address, size) == mapped;
}

template <typename T>
GALAXY_ALWAYS_INLINE T load_mapped(std::uint32_t address) noexcept {
    static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
    T value{};
    std::memcpy(
        &value,
        reinterpret_cast<const void*>(flat_guest_memory::kGuestBase + address),
        sizeof(value));
    if constexpr (sizeof(T) == 2u) {
        return byte_swap_u16(value);
    } else if constexpr (sizeof(T) == 4u) {
        return byte_swap_u32(value);
    } else if constexpr (sizeof(T) == 8u) {
        return byte_swap_u64(value);
    } else {
        static_assert(sizeof(T) == 1u);
        return value;
    }
}

GALAXY_ALWAYS_INLINE std::uint8_t read_u8(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return eligible(memory, address, 1u)
        ? load_mapped<std::uint8_t>(address)
        : guest_load_u8(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint16_t read_u16(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return eligible(memory, address, 2u)
        ? load_mapped<std::uint16_t>(address)
        : guest_load_u16(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint32_t read_u32(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    // The checked helper owns an optional load trace; keep that exact path
    // whenever it is enabled rather than silently dropping observations.
    return eligible(memory, address, 4u) &&
                   !trace_fileloader_stack_enabled()
        ? load_mapped<std::uint32_t>(address)
        : guest_load_u32(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint64_t read_u64(
    GuestMemoryV1* memory,
    std::uint32_t address,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    return eligible(memory, address, 8u)
        ? load_mapped<std::uint64_t>(address)
        : guest_load_u64(memory, address, services, guest_pc);
}

}  // namespace galaxy::host::flat_guest_read
