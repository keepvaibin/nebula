// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// a*b+c rounded once (IEEE fusedMultiplyAdd, round to nearest), always native.
//
// Use these instead of std::fma/fmaf. On an SSE2 build std::fma is a call into
// the C runtime, and on a CPU without FMA3 the C runtime computes it with an
// integer soft-float routine. The THP movie decoder makes ~290,000 fused
// operations per 640x368 frame; through that routine a frame took 150-220 ms
// instead of about 1 ms, and every cutscene ran at a few frames per second.
// tools/audit_repo.ps1 rejects std::fma in the runtime outside this header.
//
// FMA3 builds (clang -mfma, MSVC /arch:AVX2) use vfmadd. SSE2 builds use the
// exact sequences below. Both give the same bits for every operand the
// sequences admit; the rest go to std::fma, see fused_multiply_add_sse2(double).
//
// binary32: the product of two binary32 values is exact in binary64, so only
// the sum rounds. Narrowing that sum rounds a second time, which can differ
// from one rounding only when the sum is inexact and lies exactly on a
// binary32 rounding boundary. Then the sum is moved one binary64 ulp toward the
// exact value (rounding to odd), and the narrowing rounds once (Boldo and
// Melquiond, "Emulation of a FMA and correctly rounded sums: proved algorithms
// using rounding to odd", IEEE Trans. Computers 57(4), 2008, theorem 1).
//
// binary64: Dekker's exact product and TwoSum give a*b+c exactly as three
// binary64 terms. The two low terms are added with rounding to odd and the
// final addition rounds once (the same paper, section 5).
//
// Both assume the host rounds to nearest, which Nebula never changes. FTZ/DAZ
// (fast-float modules) cannot reach them: no admitted operand gives a
// subnormal intermediate.

#include <bit>
#include <cmath>
#include <cstdint>
#include <emmintrin.h>
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__FMA__) || (defined(_MSC_VER) && !defined(__clang__) && defined(__AVX2__))
#define GALAXY_FUSED_MULTIPLY_ADD_IS_FMA3 1
#else
#define GALAXY_FUSED_MULTIPLY_ADD_IS_FMA3 0
#endif

// Lets a function use FMA3 intrinsics in an SSE2 translation unit. MSVC needs
// nothing: it accepts every intrinsic whatever /arch is.
#if defined(__clang__) || defined(__GNUC__)
#define GALAXY_FMA3_TARGET __attribute__((target("fma")))
#else
#define GALAXY_FMA3_TARGET
#endif

// The exact sequences need every product and sum rounded on its own.
#if defined(__clang__)
#define GALAXY_FUSED_NO_CONTRACTION _Pragma("clang fp contract(off)")
#else
#define GALAXY_FUSED_NO_CONTRACTION
#endif

namespace galaxy {
namespace fused_detail {

[[nodiscard]] inline std::uint64_t step_toward(std::uint64_t bits, double error) noexcept {
    // bits is a nonzero finite binary64; one ulp further from zero when the
    // error has its sign, one ulp closer otherwise.
    return std::signbit(error) == ((bits >> 63) != 0u) ? bits + 1u : bits - 1u;
}

// x + y rounded to odd: the rounded sum when exact, otherwise whichever
// neighbour of the exact sum has an odd last bit.
[[nodiscard]] inline double add_rounded_to_odd(double x, double y) noexcept {
    GALAXY_FUSED_NO_CONTRACTION
    const double sum = x + y;
    const double y_part = sum - x;
    const double error = (x - (sum - y_part)) + (y - y_part);
    const auto bits = std::bit_cast<std::uint64_t>(sum);
    if (error == 0.0 || (bits & 1u) != 0u) return sum;
    return std::bit_cast<double>(step_toward(bits, error));
}

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
inline float narrow_sum_rounded_to_odd(double product, double addend) noexcept {
    const double sum = product + addend;
    if (!std::isfinite(sum)) return static_cast<float>(sum);
    return static_cast<float>(add_rounded_to_odd(product, addend));
}

} // namespace fused_detail

[[nodiscard]] inline float fused_multiply_add_sse2(float a, float b, float c) noexcept {
    GALAXY_FUSED_NO_CONTRACTION
    const double product = static_cast<double>(a) * static_cast<double>(b);
    const double addend = static_cast<double>(c);
    const double sum = product + addend;
    const auto bits = std::bit_cast<std::uint64_t>(sum);
    // The 29 bits below binary32 precision are not exactly one half, and the
    // magnitude is zero or at least the smallest normal binary32 (below it,
    // binary32 keeps fewer bits and the boundaries move).
    if ((bits & 0x1FFF'FFFFull) != 0x1000'0000ull &&
        (bits & 0x7FFF'FFFF'FFFF'FFFFull) - 1u >= 0x3810'0000'0000'0000ull - 1u) [[likely]] {
        return static_cast<float>(sum);
    }
    return fused_detail::narrow_sum_rounded_to_odd(product, addend);
}

[[nodiscard]] inline double fused_multiply_add_sse2(double a, double b, double c) noexcept {
    GALAXY_FUSED_NO_CONTRACTION
    const auto a_bits = std::bit_cast<std::uint64_t>(a);
    const auto b_bits = std::bit_cast<std::uint64_t>(b);
    const auto c_bits = std::bit_cast<std::uint64_t>(c);
    // A zero factor makes the product an exact (signed) zero.
    if ((a_bits << 1) == 0u || (b_bits << 1) == 0u) return a * b + c;
    // |a|, |b| in [2^-400, 2^401) and c zero or |c| in [2^-800, 2^801): the
    // splitting cannot overflow, and no partial product, error term or nonzero
    // result is subnormal. Guest values outside this never reach the fast paths.
    const auto a_exponent = static_cast<std::uint32_t>(a_bits >> 52) & 0x7FFu;
    const auto b_exponent = static_cast<std::uint32_t>(b_bits >> 52) & 0x7FFu;
    const auto c_exponent = static_cast<std::uint32_t>(c_bits >> 52) & 0x7FFu;
    if (a_exponent - 623u > 800u || b_exponent - 623u > 800u ||
        ((c_bits << 1) != 0u && c_exponent - 223u > 1600u)) [[unlikely]] {
        return std::fma(a, b, c);
    }
    // Dekker: product_high + product_low == a*b exactly (Veltkamp split at 27 bits).
    constexpr double kSplit = 134217729.0;
    const double a_scaled = a * kSplit;
    const double a_high = a_scaled - (a_scaled - a);
    const double a_low = a - a_high;
    const double b_scaled = b * kSplit;
    const double b_high = b_scaled - (b_scaled - b);
    const double b_low = b - b_high;
    const double product_high = a * b;
    const double product_low =
        ((a_high * b_high - product_high) + a_high * b_low + a_low * b_high) + a_low * b_low;
    // TwoSum: sum_high + sum_low == c + product_high exactly.
    const double sum_high = c + product_high;
    const double c_part = sum_high - product_high;
    const double sum_low = (product_high - (sum_high - c_part)) + (c - c_part);
    return sum_high + fused_detail::add_rounded_to_odd(sum_low, product_low);
}

// The instruction. A caller not compiled for FMA3 must check host_has_fma3()
// first, and in clang it is inlined only into GALAXY_FMA3_TARGET functions.
[[nodiscard]] GALAXY_FMA3_TARGET inline float fused_multiply_add_fma3(float a, float b, float c) noexcept {
    return _mm_cvtss_f32(_mm_fmadd_ss(_mm_set_ss(a), _mm_set_ss(b), _mm_set_ss(c)));
}
[[nodiscard]] GALAXY_FMA3_TARGET inline double fused_multiply_add_fma3(double a, double b, double c) noexcept {
    return _mm_cvtsd_f64(_mm_fmadd_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c)));
}

namespace fused_detail {

// FMA3 and the OS saving the AVX state (CPUID.1:ECX FMA, OSXSAVE, AVX; XCR0 SSE|AVX).
[[nodiscard]] inline bool detect_fma3() noexcept {
#if defined(_MSC_VER)
    int registers[4]{};
    __cpuid(registers, 0);
    if (registers[0] < 1) return false;
    __cpuid(registers, 1);
    constexpr int kRequired = (1 << 12) | (1 << 27) | (1 << 28);
    if ((registers[2] & kRequired) != kRequired) return false;
    return (_xgetbv(0) & 0x6u) == 0x6u;
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("fma") && __builtin_cpu_supports("avx");
#endif
}

// Read on every fused operation of an SSE2 build: a plain load, set once when the image loads.
inline const bool kHostHasFma3 = detect_fma3();

} // namespace fused_detail

[[nodiscard]] inline bool host_has_fma3() noexcept { return fused_detail::kHostHasFma3; }

// An SSE2 build still runs the instruction on a CPU that has FMA3 (AMD FX and A-series have FMA3 but
// no AVX2, so their game modules are SSE2 builds); the exact sequence runs only where there is none.
[[nodiscard]] inline float fused_multiply_add(float a, float b, float c) noexcept {
#if GALAXY_FUSED_MULTIPLY_ADD_IS_FMA3
    return fused_multiply_add_fma3(a, b, c);
#else
    if (fused_detail::kHostHasFma3) return fused_multiply_add_fma3(a, b, c);
    return fused_multiply_add_sse2(a, b, c);
#endif
}

[[nodiscard]] inline double fused_multiply_add(double a, double b, double c) noexcept {
#if GALAXY_FUSED_MULTIPLY_ADD_IS_FMA3
    return fused_multiply_add_fma3(a, b, c);
#else
    if (fused_detail::kHostHasFma3) return fused_multiply_add_fma3(a, b, c);
    return fused_multiply_add_sse2(a, b, c);
#endif
}

} // namespace galaxy
