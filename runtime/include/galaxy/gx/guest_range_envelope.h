#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace galaxy::gx::detail {

// Quick rejection filter for overlap tests, never an eviction index: per address band (MEM1,
// MEM2, above), the envelope of every range included since clear(). Removed ranges may leave
// it too large, which is harmless. Separate bands keep MEM1 writes from matching a cache that
// holds only MEM2 ranges across the hole between them.
class ConservativeGuestRangeEnvelope {
public:
    void clear() noexcept { bands_ = {}; }

    void include(std::uint32_t address, std::uint32_t size) noexcept {
        const std::uint64_t begin = address;
        const std::uint64_t end = begin + size;
        constexpr std::uint64_t limits[] = {
            0u, 0x10000000u, 0x20000000u,
            std::numeric_limits<std::uint64_t>::max()};
        for (unsigned i = 0; i < bands_.size(); ++i) {
            const auto clipped_begin = std::max(begin, limits[i]);
            const auto clipped_end = std::min(end, limits[i + 1u]);
            if (clipped_begin < clipped_end) {
                bands_[i].begin = std::min(bands_[i].begin, clipped_begin);
                bands_[i].end = std::max(bands_[i].end, clipped_end);
            }
        }
    }

    [[nodiscard]] bool may_overlap(
        std::uint32_t address, std::uint32_t size) const noexcept {
        if (size == 0u) return false;
        const std::uint64_t begin = address;
        const std::uint64_t end = begin + size;
        for (const auto& band : bands_) {
            if (band.begin < band.end && begin < band.end && band.begin < end) return true;
        }
        return false;
    }

    // `ranges` must be sorted and non-overlapping, as for the cache's exact overlap search;
    // Range has guest_addr and size.
    template <class Range>
    [[nodiscard]] bool may_overlap(std::span<const Range> ranges) const noexcept {
        if (ranges.empty()) return false;
        for (const auto& band : bands_) {
            if (band.begin >= band.end) continue;
            auto it = std::lower_bound(ranges.begin(), ranges.end(), band.begin,
                [](const Range& range, std::uint64_t begin) {
                    return static_cast<std::uint64_t>(range.guest_addr) + range.size <= begin;
                });
            for (; it != ranges.end() && it->guest_addr < band.end; ++it) {
                if (it->size != 0u) return true;
            }
        }
        return false;
    }

private:
    struct Band {
        std::uint64_t begin = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t end = 0u;
    };
    std::array<Band, 3> bands_{};
};

} // namespace galaxy::gx::detail
