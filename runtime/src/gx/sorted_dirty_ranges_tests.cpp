// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/sorted_dirty_ranges.h"
#include <array>
#include <cstdio>
#include <vector>

struct Range { std::uint32_t guest_addr; std::uint32_t size; };

bool linear_overlap(std::span<const Range> ranges, std::uint32_t address, std::uint32_t size) {
    if (size == 0u) return false;
    const std::uint64_t end = static_cast<std::uint64_t>(address) + size;
    for (const auto& range : ranges) {
        if (range.size != 0u && range.guest_addr < end &&
            address < static_cast<std::uint64_t>(range.guest_addr) + range.size) return true;
    }
    return false;
}

int main() {
    std::uint32_t random = 0x91A7235Du;
    const auto next = [&random]() { random = random * 1664525u + 1013904223u; return random; };
    std::uint64_t cases = 0u;
    const auto check = [&cases](std::span<const Range> ranges, std::uint32_t address, std::uint32_t size) {
        ++cases;
        const bool expected = linear_overlap(ranges, address, size);
        const bool actual = galaxy::gx::sorted_dirty_ranges_overlap(ranges, address, size);
        if (expected != actual) {
            std::fprintf(stderr, "overlap mismatch address=%08X size=%08X ranges=%zu\n", address, size, ranges.size());
            return false;
        }
        return true;
    };
    for (unsigned count : {0u, 1u, 32u, 128u, 1024u}) {
        std::vector<Range> ranges;
        std::uint32_t address = 0u;
        for (unsigned i = 0u; i < count; ++i) {
            address += 1u + next() % 512u;
            const auto size = 1u + next() % 2048u;
            ranges.push_back({address, size});
            address += size;
        }
        for (const auto& range : ranges) {
            for (const auto start : {range.guest_addr - 1u, range.guest_addr,
                     range.guest_addr + range.size - 1u, range.guest_addr + range.size}) {
                for (const auto size : {0u, 1u, 2u, 0xFFFFFFFFu}) {
                    if (!check(ranges, start, size)) return 1;
                }
            }
        }
        for (unsigned i = 0u; i < 40000u; ++i) {
            const auto start = next() % (address + 4096u);
            const auto size = next() % 4096u;
            if (!check(ranges, start, size)) return 1;
        }
    }
    const std::array<Range, 3u> regions{{{0u, 128u}, {0x10000000u, 128u}, {0xFFFFFFFEu, 2u}}};
    for (const auto start : {0u, 127u, 128u, 0x01800000u, 0x10000000u, 0x1000007Fu, 0xFFFFFFFDu, 0xFFFFFFFFu}) {
        for (const auto size : {0u, 1u, 2u, 0xFFFFFFFFu}) {
            if (!check(regions, start, size)) return 1;
        }
    }
    std::printf("sorted dirty ranges: %llu differential boundary/random cases passed\n",
        static_cast<unsigned long long>(cases));
    return 0;
}
