// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace galaxy::gx {

// Input consists of nonempty physical dirty ranges, sorted and coalesced by
// GxBackend. The caller normalizes the dependency's cached/uncached alias.
// Nonoverlapping ranges have monotonically increasing ends, so the first end
// past the dependency start is the only candidate needed for an overlap.
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
