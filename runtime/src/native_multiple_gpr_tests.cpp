#include "galaxy/native_api.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
struct Stop {};
bool record_output = false;
std::uint32_t checked = 0u;
struct Observation {
    std::uint32_t address{}, size{};
    std::array<std::uint32_t, 32> gpr{};
    std::array<std::byte, 2048> ram{};
    std::array<std::uint64_t, 8> shared{}, cpu{};
    std::array<std::byte, 4> value{};
    std::uint64_t summary{};
    bool operator==(const Observation&) const = default;
};
struct Fixture {
    galaxy::PpcContext context{};
    galaxy::GuestMemoryV1 memory{};
    galaxy::NativeServicesV1 services{};
    std::array<std::byte, 2048> ram{}, alternate{};
    std::array<std::atomic_uint64_t, 8> shared{};
    std::array<std::atomic_uint64_t, 1> summary{};
    std::array<std::uint64_t, 8> cpu{};
    std::array<galaxy::GuestMemoryRegionV1, 2> regions{};
    std::uint32_t mode{}, calls{}, fault_pc{};
    std::array<Observation, 32> observations{};
    std::array<char, 80> fault{};
    std::uint64_t observed{1469598103934665603ull};
    void observe(std::uint32_t address, std::uint32_t size, const std::byte* value) {
        auto mix = [&](std::uint64_t x) { observed = (observed ^ x) * 1099511628211ull; };
        mix(address); mix(size);
        for (const auto reg : context.gpr) mix(reg);
        for (const auto byte : ram) mix(std::to_integer<unsigned int>(byte));
        for (const auto& word : shared) mix(word.load());
        for (const auto word : cpu) mix(word);
        for (std::uint32_t i = 0; value != nullptr && i < size; ++i)
            mix(std::to_integer<unsigned int>(value[i]));
        auto& o = observations.at(calls);
        o.address = address; o.size = size; o.ram = ram; o.cpu = cpu;
        std::copy(std::begin(context.gpr), std::end(context.gpr), o.gpr.begin());
        for (std::size_t i = 0; i < shared.size(); ++i) o.shared[i] = shared[i].load();
        o.summary = summary[0].load();
        if (value != nullptr) std::memcpy(o.value.data(), value, size);
        ++calls;
    }
    static void notify(void* user, std::uint32_t address, std::uint32_t size) {
        auto& f = *static_cast<Fixture*>(user); f.observe(address, size, nullptr);
        if (f.mode == 5u) {
            f.context.gpr[31] ^= 0xABCDEF12u;
            f.memory.fast_regions[address >> 28].host_base = f.alternate.data();
        }
    }
    static bool read(void* user, std::uint32_t address, std::uint32_t size, std::byte* data) {
        auto& f = *static_cast<Fixture*>(user);
        if (f.mode != 9u && f.mode != 10u) return false;
        for (std::uint32_t i = 0; i < size; ++i) data[i] = std::byte((address + i) & 255u);
        f.observe(address, size, data); f.context.gpr[31] ^= 0x11111111u;
        return true;
    }
    static bool write(void* user, std::uint32_t address, std::uint32_t size, const std::byte* data) {
        auto& f = *static_cast<Fixture*>(user);
        if (f.mode != 9u && f.mode != 10u && f.mode != 20u) return false;
        f.observe(address, size, data); f.context.gpr[31] ^= 0x22222222u;
        return true;
    }
    static void fatal(void* user, std::uint32_t pc, const char* message) {
        auto& f = *static_cast<Fixture*>(user); f.fault_pc = pc;
        std::snprintf(f.fault.data(), f.fault.size(), "%s", message); throw Stop{};
    }
    Fixture(std::uint32_t selected, std::uint32_t address) : mode(selected) {
        for (std::uint32_t r = 0; r < 32u; ++r) context.gpr[r] = 0x12340000u + r * 7919u;
        for (std::size_t i = 0; i < ram.size(); ++i) {
            ram[i] = std::byte((i * 13u + 9u) & 255u); alternate[i] = std::byte((i * 7u + 1u) & 255u);
        }
        memory.user = this; memory.notify_write = notify;
        memory.read_device = read; memory.write_device = write;
        services.user = this; services.fatal = fatal;
        auto& fast = memory.fast_regions[address >> 28]; fast = {2048u, ram.data()};
        const std::uint32_t base = address & 0xF0000000u;
        regions[0] = {base, 2048u, ram.data()}; memory.regions = regions.data(); memory.region_count = 1u;
        if (mode != 0u && mode != 5u && mode != 9u && mode != 10u) {
            memory.dirty_page_words = shared.data(); memory.dirty_tracked_base = base & 0x1FFFFFFFu;
            memory.dirty_tracked_size = 2048u; memory.dirty_page_shift = mode == 1u ? 2u : 5u;
            memory.dirty_page_word_count = 8u; memory.dirty_word_summary = summary.data(); memory.dirty_word_summary_count = 1u;
        }
        if (mode == 3u || mode == 16u || mode == 21u) {
            memory.cpu_dirty_page_words = cpu.data();
            memory.cpu_dirty_tracked_base = (base & 0x1FFFFFFFu) + (mode == 16u ? 80u : 0u);
            memory.cpu_dirty_tracked_size = mode == 21u ? 1u : 2048u;
            memory.cpu_dirty_page_shift = 3u; memory.cpu_dirty_page_word_count = mode == 21u ? 0u : 8u;
        }
        if (mode == 4u) { memory.dirty_tracked_base += 80u; memory.dirty_tracked_size = 64u; }
        if (mode == 6u) fast.host_base = nullptr;
        if (mode == 7u) fast.size = 76u;
        if (mode == 8u) { fast.size = 76u; regions[0].size = 76u; }
        if (mode == 9u || mode == 10u) { fast = {}; memory.region_count = 0u; }
        if (mode == 12u) memory.dirty_page_word_count = 0u;
        if (mode == 13u) memory.dirty_page_shift = 32u;
        if (mode == 15u) { memory.dirty_word_summary = nullptr; memory.dirty_word_summary_count = 0u; }
        if (mode == 22u) memory.dirty_word_summary_count = 0u;
        if (mode == 17u) { shared[0].store(0x8000000000000001ull); summary[0].store(1u); }
        if (mode == 18u || mode == 19u) {
            // Live sequential reads/writes must survive guest RAM overlapping GPR bytes.
            fast = {128u, reinterpret_cast<std::byte*>(context.gpr)};
            regions[0] = {base, 128u, reinterpret_cast<std::byte*>(context.gpr)};
        }
        if (mode == 20u) {
            // The first store disables the fast mapping. Later stores must dispatch,
            // rather than using a span captured before the metadata was overwritten.
            fast = {128u, reinterpret_cast<std::byte*>(&fast.size)};
            context.gpr[28] = 0u;
        }
    }
    void print(std::uint32_t first, std::uint32_t address, bool store) const {
        std::printf("%u %u %08x %u %u %08x %016llx %s\n", mode, first, address, store,
            calls, fault_pc, static_cast<unsigned long long>(observed), fault.data());
        for (auto r : context.gpr) std::printf("%08x", r); std::puts("");
        for (auto b : ram) std::printf("%02x", std::to_integer<unsigned int>(b)); std::puts("");
        for (auto b : alternate) std::printf("%02x", std::to_integer<unsigned int>(b)); std::puts("");
        for (const auto& w : shared) std::printf("%016llx", static_cast<unsigned long long>(w.load()));
        for (auto w : cpu) std::printf("%016llx", static_cast<unsigned long long>(w));
        std::printf("%016llx\n", static_cast<unsigned long long>(summary[0].load()));
    }
};
template <std::uint32_t First> void execute(Fixture& f, std::uint32_t address, bool store, bool original) {
    try {
        if (original) {
            for (std::uint32_t r = First; r < 32u; ++r) {
                if (store) galaxy::guest_store_u32(&f.memory, address + (r - First) * 4u,
                    f.context.gpr[r], &f.services, 0x80004000u);
                else f.context.gpr[r] = galaxy::guest_load_u32(&f.memory,
                    address + (r - First) * 4u, &f.services, 0x80004000u);
            }
        } else {
#if !defined(GALAXY_TEST_ORIGINAL_MULTIPLE_GPRS)
            if (store) galaxy::guest_store_multiple_gprs<First>(&f.context, &f.memory, address, &f.services, 0x80004000u);
            else galaxy::guest_load_multiple_gprs<First>(&f.context, &f.memory, address, &f.services, 0x80004000u);
#endif
        }
    } catch (const Stop&) {}
}
template <std::uint32_t First> void run(std::uint32_t mode, std::uint32_t address, bool store) {
    Fixture actual(mode, address);
#if defined(GALAXY_TEST_ORIGINAL_MULTIPLE_GPRS)
    execute<First>(actual, address, store, true);
#else
    execute<First>(actual, address, store, false);
    Fixture expected(mode, address);
    execute<First>(expected, address, store, true);
    bool equal = std::memcmp(&actual.context, &expected.context, sizeof(actual.context)) == 0 &&
        actual.ram == expected.ram && actual.alternate == expected.alternate &&
        actual.cpu == expected.cpu && actual.calls == expected.calls &&
        actual.fault_pc == expected.fault_pc && actual.fault == expected.fault &&
        actual.observations == expected.observations;
    for (std::size_t i = 0; i < actual.shared.size(); ++i)
        equal &= actual.shared[i].load() == expected.shared[i].load();
    equal &= actual.summary[0].load() == expected.summary[0].load();
    if (!equal) {
        std::fprintf(stderr, "FAIL mode=%u first=%u address=%08x store=%u\n", mode, First, address, store);
        std::exit(1);
    }
#endif
    ++checked;
    if (record_output) actual.print(First, address, store);
}
template <std::uint32_t First> void cases() {
    constexpr std::array<std::uint32_t, 8> aliases{0u, 0x10000000u, 0x80000000u, 0x90000000u,
        0xC0000000u, 0xD0000000u, 0x40000000u, 0xA0000000u};
    for (std::uint32_t mode = 0; mode < 23u; ++mode) {
        for (auto base : aliases) for (auto offset : {64u, 65u, 124u, 2040u}) {
            auto address = base + offset;
            if (mode == 9u) address = 0xCC008000u;
            if (mode == 10u) address = 0xFFFFFFFCu;
            if (mode == 18u || mode == 19u || mode == 20u) address = base;
            // Metadata overwrite fixture is deliberately controlled for r28..31.
            if (mode == 20u && First != 28u) continue;
            if (mode != 20u) run<First>(mode, address, false);
            run<First>(mode, address, true);
        }
    }
    if constexpr (First != 31u) cases<First + 1u>();
}
}
int main(int argc, char**) {
    record_output = argc > 1;
    cases<0u>();
    std::printf("Multi-register cases checked: %u\n", checked);
    return 0;
}
