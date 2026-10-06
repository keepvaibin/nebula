// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace galaxy::scheduling {
struct CacheCluster {
    std::uint64_t processor_mask{};
    std::uint32_t capacity_bytes{};
};
struct CachePlacement {
    std::uint64_t processor_mask{};
    std::uint32_t capacity_bytes{};
};

// Optimize only an unambiguous heterogeneous last-level-cache topology.
// Never widen the caller's allowed mask or guess processor numbering.
inline std::optional<CachePlacement> select_largest_cache(
    std::span<const CacheCluster> clusters, std::uint64_t allowed_mask) {
    std::uint64_t covered = 0;
    CachePlacement largest{};
    std::uint32_t smallest = UINT32_MAX;
    unsigned eligible = 0;
    unsigned largest_count = 0;
    for (const auto& cluster : clusters) {
        if (cluster.processor_mask == 0 || cluster.capacity_bytes == 0 ||
            (covered & cluster.processor_mask) != 0) {
            return std::nullopt;
        }
        covered |= cluster.processor_mask;
        const auto mask = cluster.processor_mask & allowed_mask;
        if (mask == 0) continue;
        ++eligible;
        if (cluster.capacity_bytes < smallest) smallest = cluster.capacity_bytes;
        if (cluster.capacity_bytes > largest.capacity_bytes) {
            largest = {mask, cluster.capacity_bytes};
            largest_count = 1;
        } else if (cluster.capacity_bytes == largest.capacity_bytes) {
            ++largest_count;
        }
    }
    if (eligible < 2 || largest_count != 1 ||
        smallest == largest.capacity_bytes || (allowed_mask & ~covered) != 0) {
        return std::nullopt;
    }
    return largest;
}
}  // namespace galaxy::scheduling
