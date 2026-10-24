// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <bit>
#include <cstdint>

namespace galaxy::gx {

// The 64 KiB pages a range touches, folded into 64 bits: overlapping ranges always share a
// bit, and false sharing only sends work to the exact check. The address is canonical; the
// end is computed in 64 bits, as in the exact check, so it never wraps.
[[nodiscard]] constexpr std::uint64_t dependency_page_mask(
    std::uint32_t canonical_address, std::uint32_t size) noexcept {
    if (size == 0u) return 0u;
    const std::uint64_t first = static_cast<std::uint64_t>(canonical_address) >> 16u;
    const std::uint64_t last =
        (static_cast<std::uint64_t>(canonical_address) + size - 1u) >> 16u;
    const std::uint64_t count = last - first + 1u;
    if (count >= 64u) return ~std::uint64_t{0};
    return std::rotl((std::uint64_t{1} << count) - 1u,
                    static_cast<int>(first & 63u));
}

} // namespace galaxy::gx
