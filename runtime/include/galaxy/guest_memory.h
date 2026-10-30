// SPDX-License-Identifier: GPL-3.0-only
// The game's memory as the renderer reads it (the console's MEM1 and MEM2, big-endian, at their guest addresses),
// and the host callbacks the GX layer reports to.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#define GALAXY_ALWAYS_INLINE __forceinline
#define GALAXY_NOINLINE __declspec(noinline)
#else
#define GALAXY_ALWAYS_INLINE inline __attribute__((always_inline))
#define GALAXY_NOINLINE __attribute__((noinline))
#endif

namespace galaxy {

constexpr std::uint32_t byte_swap_u32(std::uint32_t value) noexcept {
    return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) | ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

// A guest address range backed by host memory.
struct GuestMemoryRegion {
    std::uint32_t guest_base{};
    std::uint32_t size{};
    std::byte* host_base{};
};

// The region for one 256 MiB segment of the guest address space (address >> 28), at offset 0 of the segment.
struct GuestMemoryFastRegion {
    std::uint32_t size{};
    std::byte* host_base{};
};

struct GuestMemory {
    std::uint32_t region_count{};
    GuestMemoryRegion* regions{};
    void* user{};
    GuestMemoryFastRegion fast_regions[16]{};
};

enum class GxLogLevel : std::uint32_t {
    Trace = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

// The host's side of the GX layer: its log and the pixel engine's completion events (GXSetDrawDone's finish
// interrupt and GXSetDrawSync's token), raised by the renderer when the GPU reaches them.
struct GxServices {
    void* user{};
    void (*log)(void* user, GxLogLevel level, const char* message){};
    void (*gx_pe_finish)(void* user){};
    void (*gx_pe_token)(void* user, std::uint16_t token, bool interrupt){};
};

// `size` bytes at `address` within one segment's region: their host address, or null.
GALAXY_ALWAYS_INLINE std::byte* resolve_guest_fast(GuestMemory* memory, std::uint32_t address,
                                                   std::uint32_t size) noexcept {
    if (memory == nullptr) {
        return nullptr;
    }
    const GuestMemoryFastRegion& region = memory->fast_regions[address >> 28];
    if (region.host_base == nullptr) {
        return nullptr;
    }
    const std::uint32_t offset = address & 0x0FFFFFFFu;
    // In 64 bits: the sum cannot wrap.
    if (static_cast<std::uint64_t>(offset) + size > region.size) {
        return nullptr;
    }
    return region.host_base + offset;
}

}  // namespace galaxy
