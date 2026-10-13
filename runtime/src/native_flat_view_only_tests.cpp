// SPDX-License-Identifier: GPL-3.0-only
// Flat-view-only module helpers (translator --flat-view-only): dcbz zeroes a
// RAM line through the flat view and a line outside RAM (the locked cache)
// through the checked region path; the lean host bodies' spans and string
// views resolve to the flat view. Built with GALAXY_FLAT_VIEW_ONLY=1; the
// flat-view-only arms are active only in a clang fastmem build, every other
// build exercises the unchanged checked arms with the same expectations.

#include "galaxy/native_api.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

// MEM1 alias 0 keeps base + address inside the buffer on every build; the
// locked cache is a separate checked region.
constexpr std::uint32_t kLine = 0x00001020u;
constexpr std::uint32_t kLockedCache = 0xE0000000u;

}  // namespace

int main() {
    std::vector<std::byte> ram(0x4000u, std::byte{0xAB});
    std::vector<std::byte> locked(0x4000u, std::byte{0xCD});
    galaxy::GuestMemoryRegionV1 regions[] = {
        {0x00000000u, 0x4000u, ram.data()},
        {kLockedCache, 0x4000u, locked.data()},
    };
    galaxy::GuestMemoryV1 memory{};
    memory.region_count = 2u;
    memory.regions = regions;
    memory.flat_guest_base = ram.data();
    memory.flat_guest_read_base = ram.data();

    // dcbz in RAM: the whole aligned line, nothing outside it.
    galaxy::fast::zero_line(ram.data(), &memory, kLine + 0x13u, nullptr, 0x80004000u);
    bool zeroed = true;
    for (std::uint32_t i = 0; i < 32u; ++i) {
        zeroed = zeroed && ram[kLine + i] == std::byte{0};
    }
    check(zeroed, "dcbz zeroes the aligned RAM line");
    check(ram[kLine - 1u] == std::byte{0xAB} && ram[kLine + 32u] == std::byte{0xAB},
        "dcbz leaves the neighbouring lines alone");

    // dcbz outside the RAM aliases takes the checked region path.
    galaxy::fast::zero_line(ram.data(), &memory, kLockedCache + 0x45u, nullptr, 0x80004004u);
    bool locked_zeroed = true;
    for (std::uint32_t i = 0x40u; i < 0x60u; ++i) {
        locked_zeroed = locked_zeroed && locked[i] == std::byte{0};
    }
    check(locked_zeroed, "dcbz zeroes a locked-cache line through the region table");
    check(locked[0x3Fu] == std::byte{0xCD} && locked[0x60u] == std::byte{0xCD},
        "locked-cache neighbours untouched");

    // Lean spans and string views see the same bytes as the flat view.
    check(galaxy::lean_guest_span(&memory, 0x100u, 12u, nullptr, 0u) == ram.data() + 0x100u,
        "lean span resolves into the view");
    check(galaxy::cstring::view(&memory) == ram.data(), "string view is the flat view");

    if (failures != 0) {
        std::cerr << failures << " flat-view-only check(s) failed\n";
        return 1;
    }
    std::cout << "native flat-view-only tests passed (flat-view-only arms "
              << (GALAXY_FLAT_VIEW_ONLY_ACTIVE ? "active" : "inactive") << ")\n";
    return 0;
}
