// SPDX-License-Identifier: GPL-3.0-only
// Fast float helpers (GALAXY_FAST_FLOAT=1): native IEEE results, 25-bit
// multiplier rounding, one rounding to binary32 for the single forms,
// paired-single lanes, ps1 duplication under HID2.PSE and saturating fctiwz.

#include "galaxy/native_api.h"
#include "galaxy/ppc_float.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }
double f64(std::uint64_t value) { return std::bit_cast<double>(value); }
std::uint64_t single(float value) { return bits(static_cast<double>(value)); }
double round25(double value) {
    return f64(galaxy::fast::round_multiplier_25(bits(value)));
}

galaxy::PpcContext paired_context() {
    galaxy::PpcContext context{};
    context.hid2 = 0x20000000u;  // PSE
    return context;
}

void scalar_double() {
    galaxy::PpcContext context{};
    context.fpr_bits[2] = bits(1.5);
    context.fpr_bits[3] = bits(2.25);
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, false>(
        &context, 1u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[1] == bits(3.75), "fadd 1.5+2.25");
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Divide, false>(
        &context, 4u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[4] == bits(1.5 / 2.25), "fdiv");
    context.fpr_bits[5] = bits(1.0000000000000002);
    context.fpr_bits[6] = bits(3.0000000000000004);
    context.fpr_bits[7] = bits(-7.25);
    galaxy::fast::scalar_ternary<galaxy::PpcFloatTernaryOperation::MultiplyAdd, false>(
        &context, 8u, 5u, 6u, 7u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[8] == bits(std::fma(1.0000000000000002, 3.0000000000000004, -7.25)),
        "fmadd is fused");
    galaxy::fast::scalar_ternary<galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract, false>(
        &context, 9u, 5u, 6u, 7u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[9] == bits(-std::fma(1.0000000000000002, 3.0000000000000004, 7.25)),
        "fnmsub");
    // No ps1 duplication for double-precision results.
    check(context.ps1_bits[1] == 0u, "fadd leaves ps1 alone");
}

void scalar_single() {
    galaxy::PpcContext context = paired_context();
    const float left = 1.1F;
    const float right = 1.0000001F;  // full 24-bit significand
    context.fpr_bits[2] = single(left);
    context.fpr_bits[3] = single(right);
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Multiply, true>(
        &context, 1u, 2u, 3u, false, nullptr, 0x80004000u);
    const double expected = static_cast<double>(
        static_cast<float>(static_cast<double>(left) * round25(static_cast<double>(right))));
    check(context.fpr_bits[1] == bits(expected), "fmuls rounds the multiplier to 25 bits");
    check(context.ps1_bits[1] == bits(expected), "fmuls duplicates into ps1 under PSE");
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Add, true>(
        &context, 4u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[4] == single(static_cast<float>(static_cast<double>(left) + right)),
        "fadds rounds once to binary32");

    context.hid2 = 0u;
    context.ps1_bits[5] = 0xDEADBEEFu;
    galaxy::fast::scalar_binary<galaxy::PpcFloatBinaryOperation::Subtract, true>(
        &context, 5u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.ps1_bits[5] == 0xDEADBEEFu, "no ps1 duplication without PSE");

    context.fpr_bits[6] = bits(0.1);
    galaxy::fast::scalar_frsp(&context, 7u, 6u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[7] == single(0.1F), "frsp");
}

void paired() {
    galaxy::PpcContext context = paired_context();
    context.fpr_bits[2] = single(1.5F);
    context.ps1_bits[2] = single(-2.0F);
    context.fpr_bits[3] = single(0.25F);
    context.ps1_bits[3] = single(8.0F);
    galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::Add, galaxy::fast::PairedLane::Each>(
        &context, 1u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[1] == single(1.75F) && context.ps1_bits[1] == single(6.0F), "ps_add");
    galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::Multiply, galaxy::fast::PairedLane::Lane0>(
        &context, 4u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[4] == single(0.375F) && context.ps1_bits[4] == single(-0.5F), "ps_muls0");
    galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::Multiply, galaxy::fast::PairedLane::Lane1>(
        &context, 5u, 2u, 3u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[5] == single(12.0F) && context.ps1_bits[5] == single(-16.0F), "ps_muls1");

    // 25-bit multiplier rounding on a full-significand lane.
    const float dense = 1.0000001F;
    context.fpr_bits[6] = single(dense);
    context.ps1_bits[6] = single(dense);
    galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::Multiply, galaxy::fast::PairedLane::Each>(
        &context, 7u, 2u, 6u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[7] == single(static_cast<float>(1.5 * round25(static_cast<double>(dense)))),
        "ps_mul rounds the multiplier to 25 bits");

    context.fpr_bits[8] = single(3.0F);
    context.ps1_bits[8] = single(-1.0F);
    galaxy::fast::paired_ternary<galaxy::PpcFloatTernaryOperation::MultiplyAdd, galaxy::fast::PairedLane::Each>(
        &context, 9u, 2u, 3u, 8u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[9] == single(3.375F) && context.ps1_bits[9] == single(-17.0F), "ps_madd");
    galaxy::fast::paired_ternary<galaxy::PpcFloatTernaryOperation::NegativeMultiplyAdd, galaxy::fast::PairedLane::Lane0>(
        &context, 10u, 2u, 3u, 8u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[10] == single(-3.375F) && context.ps1_bits[10] == single(1.5F), "ps_nmadds0");

    galaxy::fast::paired_sum<false>(&context, 11u, 2u, 3u, 8u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[11] == single(9.5F) && context.ps1_bits[11] == single(-1.0F), "ps_sum0");
    galaxy::fast::paired_sum<true>(&context, 12u, 2u, 3u, 8u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[12] == single(3.0F) && context.ps1_bits[12] == single(9.5F), "ps_sum1");

    // Target aliasing a source reads before it writes.
    galaxy::fast::paired_binary<galaxy::PpcFloatBinaryOperation::Subtract, galaxy::fast::PairedLane::Each>(
        &context, 2u, 2u, 2u, false, nullptr, 0x80004000u);
    check(context.fpr_bits[2] == single(0.0F) && context.ps1_bits[2] == single(0.0F), "ps_sub aliasing");
}

void fctiwz() {
    galaxy::PpcContext context{};
    const auto convert = [&](double value) {
        context.fpr_bits[2] = bits(value);
        galaxy::fast::fctiwz(&context, 1u, 2u, false, nullptr, 0x80004000u);
        return context.fpr_bits[1];
    };
    check(convert(3.7) == 0xFFF8000000000003ull, "fctiwz truncates positive");
    check(convert(-3.7) == 0xFFF80000FFFFFFFDull, "fctiwz truncates negative");
    check(convert(3.0e9) == 0xFFF800007FFFFFFFull, "fctiwz saturates high");
    check(convert(-3.0e9) == 0xFFF8000080000000ull, "fctiwz saturates low");
    check(convert(std::numeric_limits<double>::quiet_NaN()) == 0xFFF8000080000000ull,
        "fctiwz NaN");
    check(convert(2147483647.5) == 0xFFF800007FFFFFFFull, "fctiwz INT_MAX edge");
    check(convert(-2147483648.5) == 0xFFF8000080000000ull, "fctiwz INT_MIN edge");
}

void resident_variants() {
    galaxy::PpcContext context = paired_context();
    std::uint64_t fpr[32]{};
    std::uint64_t ps1[32]{};
    fpr[2] = bits(2.0);
    fpr[3] = bits(0.5);
    galaxy::fast::scalar_binary_r<galaxy::PpcFloatBinaryOperation::Multiply, false>(
        &context, fpr, ps1, 1u, 2u, 3u, false, nullptr, 0x80004000u);
    check(fpr[1] == bits(1.0) && context.fpr_bits[1] == 0u, "scalar_binary_r writes the array only");
    fpr[4] = single(1.5F);
    ps1[4] = single(2.5F);
    fpr[5] = single(2.0F);
    ps1[5] = single(4.0F);
    galaxy::fast::paired_binary_r<galaxy::PpcFloatBinaryOperation::Multiply, galaxy::fast::PairedLane::Each>(
        &context, fpr, ps1, 6u, 4u, 5u, false, nullptr, 0x80004000u);
    check(fpr[6] == single(3.0F) && ps1[6] == single(10.0F), "paired_binary_r");
    fpr[7] = bits(-9.9);
    galaxy::fast::fctiwz_r(&context, fpr, ps1, 8u, 7u, false, nullptr, 0x80004000u);
    check(fpr[8] == 0xFFF80000FFFFFFF7ull, "fctiwz_r");
}

}  // namespace

int main() {
    static_assert(galaxy::fast::kFastFloat, "this test compiles the fast float mode");
    scalar_double();
    scalar_single();
    paired();
    fctiwz();
    resident_variants();
    if (failures != 0) {
        std::cerr << failures << " fast float checks failed\n";
        return 1;
    }
    std::cout << "fast float helpers ok\n";
    return 0;
}
