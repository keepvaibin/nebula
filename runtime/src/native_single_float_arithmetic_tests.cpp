// SPDX-License-Identifier: GPL-3.0-only
// Binary32 arithmetic (translator --single-float-arithmetic) and the fast float
// forms (--fast-float-forms), built with GALAXY_FAST_FLOAT=1,
// GALAXY_SINGLE_FLOAT_ARITHMETIC=1 and GALAXY_ASSUME_PAIRED_SINGLES=1.
//
// For binary32 operands, add, subtract, multiply and divide computed in
// binary32 equal the binary64 computation rounded once (with the 25-bit
// multiplier rounding, the identity on a binary32 value); the fused forms may
// differ by one unit in the last place.

#include "galaxy/native_api.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

static_assert(galaxy::fast::kSingleFloatArithmetic,
    "build with GALAXY_FAST_FLOAT=1 GALAXY_SINGLE_FLOAT_ARITHMETIC=1");
static_assert(galaxy::fast::kAssumePairedSingles, "build with GALAXY_ASSUME_PAIRED_SINGLES=1");

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

std::uint64_t widened(float value) {
    return std::bit_cast<std::uint64_t>(static_cast<double>(value));
}

double as_double(std::uint64_t bits) {
    return std::bit_cast<double>(bits);
}

// The binary64 reference: compute in binary64, round once to binary32.
float reference_binary(galaxy::PpcFloatBinaryOperation operation, float left, float right) {
    const double l = left;
    const double r = right;
    switch (operation) {
    case galaxy::PpcFloatBinaryOperation::Add:
        return static_cast<float>(l + r);
    case galaxy::PpcFloatBinaryOperation::Subtract:
        return static_cast<float>(l - r);
    case galaxy::PpcFloatBinaryOperation::Multiply:
        return static_cast<float>(l * r);
    default:
        return static_cast<float>(l / r);
    }
}

std::uint32_t ulp_distance(float a, float b) {
    const std::int32_t ia = std::bit_cast<std::int32_t>(a);
    const std::int32_t ib = std::bit_cast<std::int32_t>(b);
    return static_cast<std::uint32_t>(ia > ib ? ia - ib : ib - ia);
}

// Deterministic binary32 operands with assorted exponents and full mantissas.
float operand(std::uint32_t index) {
    std::uint32_t state = index * 2654435761u + 0x9E3779B9u;
    state ^= state >> 13;
    state *= 0x5BD1E995u;
    state ^= state >> 15;
    const std::uint32_t exponent = 120u + (state >> 27);  // 2^-7 .. 2^24
    const std::uint32_t sign = (state & 1u) << 31;
    return std::bit_cast<float>(sign | (exponent << 23) | (state & 0x007FFFFFu));
}

template <galaxy::PpcFloatBinaryOperation Operation>
void check_scalar_binary(const char* name) {
    for (std::uint32_t i = 0; i < 2000; ++i) {
        const float left = operand(2 * i);
        const float right = operand(2 * i + 1);
        galaxy::PpcContext context{};
        context.fpr_bits[1] = widened(left);
        context.fpr_bits[2] = widened(right);
        galaxy::fast::scalar_binary<Operation, true>(&context, 3u, 1u, 2u, false, nullptr, 0u);
        const float expected = reference_binary(Operation, left, right);
        if (context.fpr_bits[3] != widened(expected) || context.ps1_bits[3] != widened(expected)) {
            check(false, name);
            return;
        }
    }
}

template <galaxy::PpcFloatBinaryOperation Operation>
void check_paired_binary(const char* name) {
    for (std::uint32_t i = 0; i < 1000; ++i) {
        const float a0 = operand(4 * i);
        const float a1 = operand(4 * i + 1);
        const float b0 = operand(4 * i + 2);
        const float b1 = operand(4 * i + 3);
        galaxy::PpcContext context{};
        context.fpr_bits[1] = widened(a0);
        context.ps1_bits[1] = widened(a1);
        context.fpr_bits[2] = widened(b0);
        context.ps1_bits[2] = widened(b1);
        galaxy::fast::paired_binary<Operation, galaxy::fast::PairedLane::Each>(
            &context, 3u, 1u, 2u, false, nullptr, 0u);
        if (context.fpr_bits[3] != widened(reference_binary(Operation, a0, b0)) ||
            context.ps1_bits[3] != widened(reference_binary(Operation, a1, b1))) {
            check(false, name);
            return;
        }
    }
}

void check_fused() {
    std::uint32_t worst = 0;
    for (std::uint32_t i = 0; i < 2000; ++i) {
        const float a = operand(3 * i);
        const float c = operand(3 * i + 1);
        const float b = operand(3 * i + 2);
        galaxy::PpcContext context{};
        context.fpr_bits[1] = widened(a);
        context.fpr_bits[2] = widened(c);
        context.fpr_bits[3] = widened(b);
        galaxy::fast::scalar_ternary<galaxy::PpcFloatTernaryOperation::MultiplyAdd, true>(
            &context, 4u, 1u, 2u, 3u, false, nullptr, 0u);
        const float expected = static_cast<float>(std::fma(
            static_cast<double>(a), static_cast<double>(c), static_cast<double>(b)));
        const float actual = static_cast<float>(as_double(context.fpr_bits[4]));
        if (std::isfinite(expected) && std::fabs(expected) > 1e-30F) {
            worst = std::max(worst, ulp_distance(actual, expected));
        }
        galaxy::PpcContext paired{};
        paired.fpr_bits[1] = widened(a);
        paired.ps1_bits[1] = widened(c);
        paired.fpr_bits[2] = widened(c);
        paired.ps1_bits[2] = widened(a);
        paired.fpr_bits[3] = widened(b);
        paired.ps1_bits[3] = widened(b);
        galaxy::fast::paired_ternary<galaxy::PpcFloatTernaryOperation::MultiplyAdd,
            galaxy::fast::PairedLane::Each>(&paired, 4u, 1u, 2u, 3u, false, nullptr, 0u);
        const float paired0 = static_cast<float>(as_double(paired.fpr_bits[4]));
        const float paired1 = static_cast<float>(as_double(paired.ps1_bits[4]));
        check(paired0 == actual && paired1 == actual, "ps_madd lanes match fmadds");
    }
    check(worst <= 1u, "fmadds within one binary32 ulp of the binary64 fma");
}

void check_paired_sum() {
    galaxy::PpcContext context{};
    context.fpr_bits[1] = widened(1.25F);
    context.ps1_bits[2] = widened(2.5F);
    context.ps1_bits[3] = widened(-7.0F);
    galaxy::fast::paired_sum<false>(&context, 4u, 1u, 2u, 3u, false, nullptr, 0u);
    check(context.fpr_bits[4] == widened(3.75F) && context.ps1_bits[4] == widened(-7.0F),
        "ps_sum0");
}

void check_fast_forms() {
    galaxy::PpcContext context{};
    const auto field = [&](std::uint32_t index) { return (context.cr >> (28u - 4u * index)) & 0xFu; };
    galaxy::fast::compare_f64_cr(&context, 0u, std::bit_cast<std::uint64_t>(1.0),
        std::bit_cast<std::uint64_t>(2.0));
    check(field(0) == 0x8u, "fcmp less");
    galaxy::fast::compare_f64_cr(&context, 1u, std::bit_cast<std::uint64_t>(3.0),
        std::bit_cast<std::uint64_t>(2.0));
    check(field(1) == 0x4u, "fcmp greater");
    galaxy::fast::compare_f64_cr(&context, 7u, std::bit_cast<std::uint64_t>(-0.0),
        std::bit_cast<std::uint64_t>(0.0));
    check(field(7) == 0x2u, "fcmp equal (signed zeros)");
    galaxy::fast::compare_f64_cr(&context, 6u,
        std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()),
        std::bit_cast<std::uint64_t>(0.0));
    check(field(6) == 0x1u, "fcmp unordered");
    check(field(0) == 0x8u && field(1) == 0x4u, "other fields kept");
    check(context.fpscr == 0u, "no FPSCR update");

    check(galaxy::fast::reciprocal_single_bits(std::bit_cast<std::uint64_t>(4.0)) == widened(0.25F),
        "fres(4)");
    check(as_double(galaxy::fast::reciprocal_single_bits(std::bit_cast<std::uint64_t>(3.0))) ==
            static_cast<double>(1.0F / 3.0F),
        "fres(3) is binary32");
    check(as_double(galaxy::fast::reciprocal_sqrt_bits(std::bit_cast<std::uint64_t>(4.0))) == 0.5,
        "frsqrte(4)");
    check(std::isinf(as_double(galaxy::fast::reciprocal_sqrt_bits(std::bit_cast<std::uint64_t>(0.0)))),
        "frsqrte(0) is infinite");
    check(std::isnan(as_double(galaxy::fast::reciprocal_sqrt_bits(std::bit_cast<std::uint64_t>(-1.0)))),
        "frsqrte(-1) is NaN");
    check(galaxy::fast::reciprocal_sqrt_single_bits(std::bit_cast<std::uint64_t>(16.0)) == widened(0.25F),
        "ps_rsqrte(16)");

    check(galaxy::fast::select_nonnegative(std::bit_cast<std::uint64_t>(0.0)), "fsel +0");
    check(galaxy::fast::select_nonnegative(std::bit_cast<std::uint64_t>(-0.0)), "fsel -0");
    check(galaxy::fast::select_nonnegative(std::bit_cast<std::uint64_t>(2.0)), "fsel positive");
    check(!galaxy::fast::select_nonnegative(std::bit_cast<std::uint64_t>(-2.0)), "fsel negative");
    check(!galaxy::fast::select_nonnegative(
              std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN())),
        "fsel NaN");

    // HID2 left at zero: the paired-mode test is gone (it would fault).
    galaxy::require_paired_single_mode(&context, true, nullptr, 0u);
}

}  // namespace

int main() {
    check_scalar_binary<galaxy::PpcFloatBinaryOperation::Add>("fadds");
    check_scalar_binary<galaxy::PpcFloatBinaryOperation::Subtract>("fsubs");
    check_scalar_binary<galaxy::PpcFloatBinaryOperation::Multiply>("fmuls");
    check_scalar_binary<galaxy::PpcFloatBinaryOperation::Divide>("fdivs");
    check_paired_binary<galaxy::PpcFloatBinaryOperation::Add>("ps_add");
    check_paired_binary<galaxy::PpcFloatBinaryOperation::Multiply>("ps_mul");
    check_paired_binary<galaxy::PpcFloatBinaryOperation::Divide>("ps_div");
    check_fused();
    check_paired_sum();
    check_fast_forms();
    if (failures != 0) {
        std::cerr << failures << " single-float-arithmetic check(s) failed\n";
        return 1;
    }
    std::cout << "native single-float-arithmetic and fast-float-forms tests passed\n";
    return 0;
}
