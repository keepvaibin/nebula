// SPDX-License-Identifier: GPL-3.0-only
// Hardware float conversions (translator --hw-float-conversions): lfs, stfs
// and the binary32 lanes of psq_l/psq_st through cvtss2sd/cvtsd2ss. Ordinary
// binary32 values agree with the integer conversions in both directions; a
// binary64 value outside binary32 rounds instead of being sliced.

#include "galaxy/native_api.h"

#include <bit>
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

// Guest RAM: MEM1 alias 0x00000000, so base + address stays inside the buffer
// on every build (the flat span test admits it).
constexpr std::uint32_t kAddress = 0x00001000u;

struct Ram {
    std::vector<std::byte> bytes = std::vector<std::byte>(0x2000u);
    galaxy::GuestMemoryV1 memory{};
    Ram() {
        memory.flat_guest_base = bytes.data();
        memory.flat_guest_read_base = bytes.data();
    }
    std::byte* base() { return bytes.data(); }
    void put(std::uint32_t address, std::uint32_t value) {
        const std::uint32_t raw = galaxy::byte_swap_u32(value);
        std::memcpy(bytes.data() + address, &raw, sizeof(raw));
    }
    std::uint32_t get(std::uint32_t address) const {
        std::uint32_t raw = 0u;
        std::memcpy(&raw, bytes.data() + address, sizeof(raw));
        return galaxy::byte_swap_u32(raw);
    }
};

galaxy::PpcContext paired_context() {
    galaxy::PpcContext context{};
    context.hid2 = 0xA0000000u;  // LSQE | PSE
    return context;
}

void scalar_round_trip() {
    Ram ram;
    galaxy::PpcContext context = paired_context();
    const std::uint32_t samples[] = {
        0x3F800000u, 0xBF800000u, 0x00000000u, 0x80000000u, 0x7F7FFFFFu,
        0x00800000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x42F6E979u};
    for (const std::uint32_t value : samples) {
        ram.put(kAddress, value);
        galaxy::fast::load_fpr_single(
            ram.base(), &context, 1u, &ram.memory, kAddress, nullptr, 0x80004000u);
        galaxy::fast::load_fpr_single_hw(
            ram.base(), &context, 2u, &ram.memory, kAddress, nullptr, 0x80004000u);
        check(context.fpr_bits[2] == context.fpr_bits[1], "lfs hw widening matches");
        check(context.ps1_bits[2] == context.fpr_bits[1], "lfs hw fills ps1");
        galaxy::fast::store_fpr_single_hw(
            ram.base(), &context, 2u, &ram.memory, kAddress + 8u, nullptr, 0x80004004u);
        check(ram.get(kAddress + 8u) == value, "stfs hw round trip");
    }
}

void stfs_rounds() {
    Ram ram;
    galaxy::PpcContext context = paired_context();
    // 1 + 0.75 ulp(binary32): the hardware conversion rounds to 1 + 1 ulp,
    // the Gekko's stfs slices to 1.
    context.fpr_bits[6] = std::bit_cast<std::uint64_t>(1.0 + 0x1.8p-24);
    galaxy::fast::store_fpr_single_hw(
        ram.base(), &context, 6u, &ram.memory, kAddress, nullptr, 0x80004000u);
    check(ram.get(kAddress) == 0x3F800001u, "stfs hw rounds to nearest");
    galaxy::fast::store_fpr_single(
        ram.base(), &context, 6u, &ram.memory, kAddress + 4u, nullptr, 0x80004000u);
    check(ram.get(kAddress + 4u) == 0x3F800000u, "stfs exact slices");
}

void paired_lanes() {
    Ram ram;
    galaxy::PpcContext context = paired_context();
    ram.put(kAddress, 0x40490FDBu);
    ram.put(kAddress + 4u, 0xC0000000u);
    galaxy::fast::psq_load(
        ram.base(), &context, 3u, &ram.memory, kAddress, 0u, false, true, nullptr, 0u);
    galaxy::fast::psq_load_hw(
        ram.base(), &context, 4u, &ram.memory, kAddress, 0u, false, true, nullptr, 0u);
    check(context.fpr_bits[4] == context.fpr_bits[3], "psq_l hw lane 0");
    check(context.ps1_bits[4] == context.ps1_bits[3], "psq_l hw lane 1");
    galaxy::fast::psq_store_hw(
        ram.base(), &context, 4u, &ram.memory, kAddress + 0x20u, 0u, false, true, nullptr, 0u);
    check(ram.get(kAddress + 0x20u) == 0x40490FDBu, "psq_st hw lane 0");
    check(ram.get(kAddress + 0x24u) == 0xC0000000u, "psq_st hw lane 1");
    galaxy::fast::psq_load_hw(
        ram.base(), &context, 5u, &ram.memory, kAddress, 0u, true, true, nullptr, 0u);
    check(context.ps1_bits[5] == 0x3FF0000000000000ull, "psq_l hw one element sets ps1 to 1.0");
    // Quantized types keep the dequantization helpers.
    context.gqr[1] = 0x00040000u;  // load type u8, scale 0
    ram.put(kAddress + 0x40u, 0x07FF0000u);
    galaxy::fast::psq_load_hw(
        ram.base(), &context, 6u, &ram.memory, kAddress + 0x40u, 1u, false, true, nullptr, 0u);
    check(context.fpr_bits[6] == std::bit_cast<std::uint64_t>(7.0), "psq_l hw u8 lane 0");
    check(context.ps1_bits[6] == std::bit_cast<std::uint64_t>(255.0), "psq_l hw u8 lane 1");
}

void resident_variants() {
    Ram ram;
    galaxy::PpcContext context = paired_context();
    std::uint64_t fpr[32]{};
    std::uint64_t ps1[32]{};
    ram.put(kAddress, 0x40490FDBu);
    ram.put(kAddress + 4u, 0xC0000000u);
    galaxy::fast::load_fpr_single_hw_r(
        ram.base(), &context, fpr, ps1, 7u, &ram.memory, kAddress, nullptr, 0u);
    check(fpr[7] == std::bit_cast<std::uint64_t>(static_cast<double>(std::bit_cast<float>(0x40490FDBu))),
        "lfs hw_r");
    check(ps1[7] == fpr[7], "lfs hw_r fills ps1");
    galaxy::fast::store_fpr_single_hw_r(
        ram.base(), &context, fpr, ps1, 7u, &ram.memory, kAddress + 0x10u, nullptr, 0u);
    check(ram.get(kAddress + 0x10u) == 0x40490FDBu, "stfs hw_r");
    galaxy::fast::psq_load_hw_r(
        ram.base(), &context, fpr, ps1, 8u, &ram.memory, kAddress, 0u, false, true, nullptr, 0u);
    check(ps1[8] == std::bit_cast<std::uint64_t>(-2.0), "psq_l hw_r lane 1");
    galaxy::fast::psq_store_hw_r(
        ram.base(), &context, fpr, ps1, 8u, &ram.memory, kAddress + 0x30u, 0u, false, true, nullptr, 0u);
    check(ram.get(kAddress + 0x30u) == 0x40490FDBu && ram.get(kAddress + 0x34u) == 0xC0000000u,
        "psq_st hw_r");
}

}  // namespace

int main() {
    scalar_round_trip();
    stfs_rounds();
    paired_lanes();
    resident_variants();
    if (failures != 0) {
        std::cerr << failures << " hardware float conversion check(s) failed\n";
        return 1;
    }
    std::cout << "native hardware float conversion tests passed\n";
    return 0;
}
