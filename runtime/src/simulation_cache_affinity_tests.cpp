// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/simulation_cache_affinity.h"
#include <array>
#include <iostream>

int main() {
    using galaxy::scheduling::CacheCluster;
    using galaxy::scheduling::select_largest_cache;
    bool ok = true;
    const auto check = [&ok](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAILED: " << message << '\n'; ok = false; }
    };
    constexpr std::array topology{CacheCluster{0xFFFFu, 96u << 20u},
                                  CacheCluster{0xFFFF0000u, 32u << 20u}};
    auto chosen = select_largest_cache(topology, 0xFFFFFFFFu);
    check(chosen && chosen->processor_mask == 0xFFFFu &&
          chosen->capacity_bytes == (96u << 20u), "actual two-cluster topology");
    constexpr std::array reversed{topology[1], topology[0]};
    chosen = select_largest_cache(reversed, 0xFFFFFFFFu);
    check(chosen && chosen->processor_mask == 0xFFFFu, "record order independent");
    chosen = select_largest_cache(topology, 0x01000005u);
    check(chosen && chosen->processor_mask == 5u, "preserves existing restrictions");
    check(!select_largest_cache(topology, 0xFFFF0000u), "no widening to excluded cache");
    check(!select_largest_cache(topology, 0xFFFFu), "already restricted to largest cache");
    check(!select_largest_cache(topology, 0u), "empty allowed set");
    constexpr std::array equal{CacheCluster{3u, 64u}, CacheCluster{12u, 64u}};
    check(!select_largest_cache(equal, 15u), "homogeneous caches leave scheduler alone");
    constexpr std::array tied{CacheCluster{3u, 96u}, CacheCluster{12u, 96u}, CacheCluster{48u, 32u}};
    check(!select_largest_cache(tied, 63u), "no arbitrary choice among largest caches");
    constexpr std::array overlap{CacheCluster{3u, 96u}, CacheCluster{6u, 32u}};
    check(!select_largest_cache(overlap, 7u), "overlapping topology rejected");
    check(!select_largest_cache(equal, 31u), "uncovered allowed processor rejected");
    constexpr std::array invalid{CacheCluster{0u, 96u}, CacheCluster{15u, 32u}};
    check(!select_largest_cache(invalid, 15u), "invalid empty cluster rejected");
    constexpr std::array zero_capacity{CacheCluster{3u, 0u}, CacheCluster{12u, 32u}};
    check(!select_largest_cache(zero_capacity, 15u), "unknown capacity rejected");
    constexpr std::array upper{CacheCluster{0xF000000000000000ull, 96u},
                              CacheCluster{0x0F00000000000000ull, 32u}};
    chosen = select_largest_cache(upper, 0xFF00000000000000ull);
    check(chosen && chosen->processor_mask == 0xF000000000000000ull,
          "full64bit mask without truncation");
    check(!select_largest_cache(std::span<const CacheCluster>{}, 1u), "empty topology");
    if (ok) std::cout << "simulation cache affinity tests passed\n";
    return ok ? 0 : 1;
}
