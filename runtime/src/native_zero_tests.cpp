#include "galaxy/native_api.h"
#include <array>
#include <cstdio>

namespace {
constexpr std::uint32_t base = 0x80000000u, pc = 0x80001234u;
struct Fault { std::uint32_t caller; };
struct Probe {
    std::array<std::byte, 1088> bytes;
    std::uint32_t offset{}, size{}, notifications{};
    bool published_complete{true};
};

void notify(void* user, std::uint32_t address, std::uint32_t size) {
    auto& p = *static_cast<Probe*>(user);
    ++p.notifications;
    p.published_complete &= address == base + p.offset && size == p.size;
    for (std::uint32_t i = 0; i < p.bytes.size(); ++i) {
        const auto expected = i >= p.offset && i - p.offset < p.size ?
            std::byte{0} : std::byte{0xa5};
        p.published_complete &= p.bytes[i] == expected;
    }
}
}  // namespace

int main() {
    Probe probe{};
    galaxy::GuestMemoryRegionV1 region{base, static_cast<std::uint32_t>(probe.bytes.size()), probe.bytes.data()};
    galaxy::GuestMemoryV1 memory{};
    memory.user = &probe;
    memory.regions = &region;
    memory.region_count = 1;
    memory.notify_write = notify;
    galaxy::NativeServicesV1 services{};
    services.fatal = [](void*, std::uint32_t caller, const char*) { throw Fault{caller}; };
    bool passed = true;
    for (bool fast : {false, true}) {
        memory.fast_regions[8] = {};
        if (fast) {
            memory.fast_regions[8].host_base = probe.bytes.data();
            memory.fast_regions[8].size = region.size;
        }
        for (std::uint32_t offset : {0u, 1u, 31u, 32u}) {
            for (std::uint32_t size : {0u, 1u, 31u, 32u, 33u, 64u, 511u, 1024u}) {
                probe.bytes.fill(std::byte{0xa5});
                probe.offset = offset; probe.size = size; probe.notifications = 0u;
                galaxy::guest_zero(&memory, base + offset, size, &services, pc);
                passed &= probe.notifications == (size != 0u ? 1u : 0u) && probe.published_complete;
                for (std::uint32_t i = 0; i < probe.bytes.size(); ++i)
                    passed &= probe.bytes[i] == (i >= offset && i - offset < size ?
                        std::byte{0} : std::byte{0xa5});
            }
        }
        probe.bytes.fill(std::byte{0xa5}); probe.notifications = 0u;
        galaxy::guest_zero(&memory, base + region.size, 0u, &services, pc);
        passed &= probe.notifications == 0u;
        bool faulted = false;
        try { galaxy::guest_zero(&memory, base + region.size - 1u, 2u, &services, pc); }
        catch (Fault f) { faulted = f.caller == pc; }
        passed &= faulted && probe.notifications == 0u;
        for (auto byte : probe.bytes) passed &= byte == std::byte{0xa5};
    }
    std::puts(passed ? "Guest zero span/publication checks passed" : "Guest zero checks FAILED");
    return passed ? 0 : 1;
}
