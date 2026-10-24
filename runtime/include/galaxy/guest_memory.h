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

// Sets the bits of pages `first_page`..`last_page` in a dirty bitmap (one bit per page, 64 per word).
template <typename Word>
GALAXY_NOINLINE inline void guest_mark_dirty_page_words(Word* words, std::uint32_t first_page,
                                                        std::uint32_t last_page) {
    const auto first_word = first_page / 64u;
    const auto last_word = last_page / 64u;
    for (std::uint32_t word = first_word;; ++word) {
        const auto low = word == first_word ? first_page % 64u : 0u;
        const auto high = word == last_word ? last_page % 64u : 63u;
        const std::uint64_t mask = (UINT64_MAX << low) & (UINT64_MAX >> (63u - high));
        if constexpr (std::is_same_v<Word, std::atomic_uint64_t>) {
            if ((words[word].load(std::memory_order_relaxed) & mask) != mask)
                words[word].fetch_or(mask, std::memory_order_relaxed);
        } else {
            words[word] |= mask;
        }
        if (word == last_word) break;
    }
}

// Sets the summary bit covering page word `word_index` (one bit per 64-bit page word), so a drain can skip empty
// groups of words. The load first keeps a repeated publication into the same group free of locked writes.
GALAXY_ALWAYS_INLINE void guest_publish_dirty_word_summary(std::atomic_uint64_t* summary,
                                                           std::uint32_t word_index) {
    if (summary == nullptr) {
        return;
    }
    std::atomic_uint64_t& group = summary[word_index / 64u];
    const std::uint64_t bit = 1ull << (word_index % 64u);
#if defined(_MSC_VER) && defined(_M_X64)
    const std::uint64_t loaded = *reinterpret_cast<const volatile std::uint64_t*>(&group);
    _ReadWriteBarrier();
    if ((loaded & bit) == 0u) {
        (void)_InterlockedOr64(reinterpret_cast<volatile long long*>(&group), static_cast<long long>(bit));
    }
#else
    if ((group.load(std::memory_order_relaxed) & bit) == 0u) {
        group.fetch_or(bit, std::memory_order_relaxed);
    }
#endif
}

}  // namespace galaxy
