// SPDX-License-Identifier: GPL-3.0-only
// a*b+c rounded once (IEEE fusedMultiplyAdd, round to nearest) without the C runtime.
#pragma once

// Use these instead of std::fma/fmaf: on an SSE2 build std::fma calls the C runtime, which on a CPU without FMA3
// computes it with an integer soft-float routine, far too slow for the THP movie decoder's hundreds of thousands of
// fused operations a frame. tools/audit_repo.ps1 rejects std::fma in the runtime outside this header.
//
// AArch64 has the instruction in its base set (fused_multiply_add: fmadd). x86 has fused_multiply_add_fma3 (vfmadd,
// on CPUs with FMA3) and fused_multiply_add_sse2, an exact sequence of plain binary64 arithmetic with the same result
// on any CPU.
//
// The product of two binary32 values is exact in binary64, so only the sum rounds. Narrowing that sum rounds a
// second time, which can differ from one rounding only when the sum is inexact and lies exactly on a binary32
// rounding boundary. Then the sum is moved one binary64 ulp toward the exact value (rounding to odd), and the
// narrowing rounds once (Boldo and Melquiond, "Emulation of a FMA and correctly rounded sums: proved algorithms
// using rounding to odd", IEEE Trans. Computers 57(4), 2008, theorem 1).
//
// The sequence assumes the host rounds to nearest, which Nebula never changes. FTZ/DAZ cannot reach it: no operand
// gives a subnormal intermediate.

#include <bit>
#include <cmath>
#include <cstdint>
#if defined(__aarch64__) || defined(_M_ARM64)
#define GALAXY_FUSED_MULTIPLY_ADD_IS_BASE 1
#else
#define GALAXY_FUSED_MULTIPLY_ADD_IS_BASE 0
#include <emmintrin.h>
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

// Lets a function use FMA3 intrinsics in an SSE2 translation unit. MSVC needs nothing: it accepts every
// intrinsic whatever /arch is.
#if defined(__clang__) || defined(__GNUC__)
#define GALAXY_FMA3_TARGET __attribute__((target("fma")))
#else
#define GALAXY_FMA3_TARGET
#endif

// The exact sequence needs every product and sum rounded on its own.
#if defined(__clang__)
#define GALAXY_FUSED_NO_CONTRACTION _Pragma("clang fp contract(off)")
#else
#define GALAXY_FUSED_NO_CONTRACTION
#endif

namespace galaxy {
namespace fused_detail {

[[nodiscard]] inline std::uint64_t step_toward(std::uint64_t bits, double error) noexcept {
    // bits is a nonzero finite binary64; one ulp further from zero when the error has its sign, one ulp closer
    // otherwise.
    return std::signbit(error) == ((bits >> 63) != 0u) ? bits + 1u : bits - 1u;
}

// x + y rounded to odd: the rounded sum when exact, otherwise whichever neighbour of the exact sum has an odd last
// bit.
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
    // The 29 bits below binary32 precision are not exactly one half, and the magnitude is zero or at least the
    // smallest normal binary32 (below it, binary32 keeps fewer bits and the boundaries move).
    if ((bits & 0x1FFF'FFFFull) != 0x1000'0000ull &&
        (bits & 0x7FFF'FFFF'FFFF'FFFFull) - 1u >= 0x3810'0000'0000'0000ull - 1u) [[likely]] {
        return static_cast<float>(sum);
    }
    return fused_detail::narrow_sum_rounded_to_odd(product, addend);
}

#if GALAXY_FUSED_MULTIPLY_ADD_IS_BASE

// The x86 extension; AArch64's fmadd needs no check.
[[nodiscard]] inline bool host_has_fma3() noexcept { return false; }

[[nodiscard]] inline float fused_multiply_add(float a, float b, float c) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return std::fma(a, b, c);
#else
    return __builtin_fmaf(a, b, c);
#endif
}

#else

// The instruction. A caller not compiled for FMA3 must check host_has_fma3() first, and in clang it is inlined
// only into GALAXY_FMA3_TARGET functions.
[[nodiscard]] GALAXY_FMA3_TARGET inline float fused_multiply_add_fma3(float a, float b, float c) noexcept {
    return _mm_cvtss_f32(_mm_fmadd_ss(_mm_set_ss(a), _mm_set_ss(b), _mm_set_ss(c)));
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

// Set once, when the image loads.
inline const bool kHostHasFma3 = detect_fma3();

} // namespace fused_detail

[[nodiscard]] inline bool host_has_fma3() noexcept { return fused_detail::kHostHasFma3; }

#endif

} // namespace galaxy
