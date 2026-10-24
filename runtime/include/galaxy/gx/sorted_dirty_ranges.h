// SPDX-License-Identifier: GPL-3.0-only
// Overlap test of a physical guest range against GxBackend's sorted dirty ranges.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace galaxy::gx {

// `ranges` are nonempty physical ranges, sorted and coalesced by GxBackend; the caller strips the
// cached/uncached alias from `physical_address`. Disjoint sorted ranges have increasing ends, so only the first
// range ending past the start can overlap.
template <typename Range>
[[nodiscard]] bool sorted_dirty_ranges_overlap(
    std::span<const Range> ranges,
    std::uint32_t physical_address,
    std::uint32_t size) noexcept {
    if (size == 0u) return false;
    const std::uint64_t begin = physical_address;
    const std::uint64_t end = begin + size;
    const auto it = std::lower_bound(
        ranges.begin(), ranges.end(), begin,
        [](const Range& dirty, std::uint64_t address) noexcept {
            return static_cast<std::uint64_t>(dirty.guest_addr) + dirty.size <= address;
        });
    return it != ranges.end() && static_cast<std::uint64_t>(it->guest_addr) < end;
}

} // namespace galaxy::gx
