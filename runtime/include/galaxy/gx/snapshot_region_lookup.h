// SPDX-License-Identifier: GPL-3.0-only
// Guest address lookup over a memory snapshot's region list: binary search when the list is sorted and disjoint.
#pragma once

#include "galaxy/guest_memory.h"
#include <cstdint>
#include <span>

namespace galaxy::gx::detail {

// True when every region is mapped, nonempty and inside the 32-bit address space, and the list is sorted without
// overlap. The list is not sorted here: an overlapping list keeps the generic resolver's first-match order.
[[nodiscard]] inline bool snapshot_regions_are_disjoint_sorted(
    std::span<const GuestMemoryRegion> regions) noexcept {
    std::uint64_t previous_end = 0;
    for (const auto& region : regions) {
        const std::uint64_t end = static_cast<std::uint64_t>(region.guest_base) + region.size;
        if (region.host_base == nullptr || region.size == 0u ||
            end > (UINT64_C(1) << 32u) || region.guest_base < previous_end) {
            return false;
        }
        previous_end = end;
    }
    return true;
}

// For a list that passed snapshot_regions_are_disjoint_sorted and has not changed since. A request that
// crosses into an adjacent region is unmapped: separate buffers are never joined.
[[nodiscard]] inline const std::byte* resolve_sorted_snapshot_region(
    std::span<const GuestMemoryRegion> regions,
    std::uint32_t address, std::uint32_t size) noexcept {
    if (size == 0u) return nullptr;
    std::size_t low = 0u, high = regions.size();
    while (low < high) {
        const std::size_t mid = low + (high - low) / 2u;
        if (regions[mid].guest_base <= address) low = mid + 1u;
        else high = mid;
    }
    if (low == 0u) return nullptr;
    const auto& region = regions[low - 1u];
    const std::uint64_t end = static_cast<std::uint64_t>(address) + size;
    const std::uint64_t region_end = static_cast<std::uint64_t>(region.guest_base) + region.size;
    if (region.host_base == nullptr || end > region_end) return nullptr;
    return region.host_base + (address - region.guest_base);
}

} // namespace galaxy::gx::detail
