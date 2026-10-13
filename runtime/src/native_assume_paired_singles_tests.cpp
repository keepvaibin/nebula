// SPDX-License-Identifier: GPL-3.0-only
// Paired singles always on (translator --assume-paired-singles), built with
// GALAXY_FAST_FLOAT=1 and GALAXY_ASSUME_PAIRED_SINGLES=1: with HID2 left at
// zero, psq_l/psq_st still take their native paths (no HID2 admission test),
// and single-precision results and lfs fill ps1.

#include "galaxy/native_api.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static_assert(galaxy::fast::kAssumePairedSingles, "build with GALAXY_ASSUME_PAIRED_SINGLES=1");

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

constexpr std::uint32_t kAddress = 0x00001000u;

std::uint64_t single(float value) {
    return std::bit_cast<std::uint64_t>(static_cast<double>(value));
}

}  // namespace

int main() {
    std::vector<std::byte> ram(0x2000u);
    galaxy::GuestMemoryV1 memory{};
    memory.flat_guest_base = ram.data();
    memory.flat_guest_read_base = ram.data();
    const auto put = [&](std::uint32_t address, float value) {
        const std::uint32_t raw = galaxy::byte_swap_u32(std::bit_cast<std::uint32_t>(value));
        std::memcpy(ram.data() + address, &raw, 4u);
    };
    const auto get = [&](std::uint32_t address) {
        std::uint32_t raw = 0u;
        std::memcpy(&raw, ram.data() + address, 4u);
        return std::bit_cast<float>(galaxy::byte_swap_u32(raw));
    };

    galaxy::PpcContext context{};  // HID2 = 0: no PSE, no LSQE
    put(kAddress, 1.5F);
    put(kAddress + 4u, -2.0F);
    galaxy::fast::psq_load(ram.data(), &context, 3u, &memory, kAddress, 0u, false, true, nullptr, 0u);
    check(context.fpr_bits[3] == single(1.5F) && context.ps1_bits[3] == single(-2.0F),
        "psq_l loads both lanes without the HID2 test");
    galaxy::fast::psq_store(ram.data(), &context, 3u, &memory, kAddress + 0x10u, 0u, false, true, nullptr, 0u);
    check(get(kAddress + 0x10u) == 1.5F && get(kAddress + 0x14u) == -2.0F,
        "psq_st stores both lanes without the HID2 test");

    context.fpr_bits[1] = single(3.0F);
    context.fpr_bits[2] = single(0.5F);
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Multiply, true>(
        &context, 4u, 1u, 2u, false, nullptr, 0u);
    check(context.fpr_bits[4] == single(1.5F), "fmuls result");
    check(context.ps1_bits[4] == single(1.5F), "fmuls fills ps1");
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, false>(
        &context, 5u, 1u, 2u, false, nullptr, 0u);
    check(context.ps1_bits[5] == 0u, "fadd (double) leaves ps1 alone");

    galaxy::fast::load_fpr_single(ram.data(), &context, 6u, &memory, kAddress, nullptr, 0u);
    check(context.ps1_bits[6] == single(1.5F), "lfs fills ps1");

    std::uint64_t fpr[32]{};
    std::uint64_t ps1[32]{};
    galaxy::fast::psq_load_r(ram.data(), &context, fpr, ps1, 7u, &memory, kAddress, 0u, false, true, nullptr, 0u);
    check(fpr[7] == single(1.5F) && ps1[7] == single(-2.0F), "resident psq_l");

    if (failures != 0) {
        std::cerr << failures << " assume-paired-singles check(s) failed\n";
        return 1;
    }
    std::cout << "native assume-paired-singles tests passed\n";
    return 0;
}
