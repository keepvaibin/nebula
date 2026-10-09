// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <bit>
#include <cstdint>

namespace galaxy::gx {

// Conservative set of 64-KiB pages, folded into 64 bits. A shared byte must
// produce a shared bit; collisions only send extra work to the exact check.
// Addresses are already canonicalized by the caller. Match the exact overlap
// check's wide half-open end rather than wrapping guest address arithmetic.
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
