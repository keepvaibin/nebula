// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// 128-bit integer vectors for the decode and copy kernels: SSE2/SSSE3 on x86, NEON on AArch64. Each operation is one
// instruction on both (all_equal16: two), with the same result in every lane.

#include <cstdint>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define GALAXY_SIMD_NEON 1
#else
#include <immintrin.h>
#define GALAXY_SIMD_NEON 0
#endif

namespace galaxy::simd {

#if GALAXY_SIMD_NEON

using v128 = uint8x16_t;

[[nodiscard]] inline v128 load(const void* p) noexcept { return vld1q_u8(static_cast<const std::uint8_t*>(p)); }
inline void store(void* p, v128 v) noexcept { vst1q_u8(static_cast<std::uint8_t*>(p), v); }
// Bytes in memory order.
[[nodiscard]] inline v128 bytes(std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3, std::uint8_t b4,
                                std::uint8_t b5, std::uint8_t b6, std::uint8_t b7, std::uint8_t b8, std::uint8_t b9,
                                std::uint8_t b10, std::uint8_t b11, std::uint8_t b12, std::uint8_t b13,
                                std::uint8_t b14, std::uint8_t b15) noexcept {
    const std::uint8_t values[16] = {b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15};
    return vld1q_u8(values);
}
[[nodiscard]] inline v128 splat16(std::uint16_t x) noexcept { return vreinterpretq_u8_u16(vdupq_n_u16(x)); }
[[nodiscard]] inline v128 zero() noexcept { return vdupq_n_u8(0); }
[[nodiscard]] inline v128 or_(v128 a, v128 b) noexcept { return vorrq_u8(a, b); }
[[nodiscard]] inline v128 and_(v128 a, v128 b) noexcept { return vandq_u8(a, b); }
// ~a & b.
[[nodiscard]] inline v128 andnot(v128 a, v128 b) noexcept { return vbicq_u8(b, a); }
template <int N> [[nodiscard]] inline v128 shl16(v128 v) noexcept {
    return vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(v), N));
}
template <int N> [[nodiscard]] inline v128 shr16(v128 v) noexcept {
    return vreinterpretq_u8_u16(vshrq_n_u16(vreinterpretq_u16_u8(v), N));
}
template <int N> [[nodiscard]] inline v128 shl32(v128 v) noexcept {
    return vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(v), N));
}
template <int N> [[nodiscard]] inline v128 shr32(v128 v) noexcept {
    return vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(v), N));
}
// All ones in each 16-bit lane whose top bit is set.
[[nodiscard]] inline v128 sign_mask16(v128 v) noexcept {
    return vreinterpretq_u8_s16(vshrq_n_s16(vreinterpretq_s16_u8(v), 15));
}
// 16-bit lanes a0 b0 a1 b1 a2 b2 a3 b3 / a4 b4 ... a7 b7.
[[nodiscard]] inline v128 interleave_low16(v128 a, v128 b) noexcept {
    return vreinterpretq_u8_u16(vzip1q_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
[[nodiscard]] inline v128 interleave_high16(v128 a, v128 b) noexcept {
    return vreinterpretq_u8_u16(vzip2q_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
// Byte i of the result is byte indices[i] (0-15) of v.
[[nodiscard]] inline v128 shuffle_bytes(v128 v, v128 indices) noexcept { return vqtbl1q_u8(v, indices); }
[[nodiscard]] inline v128 byteswap16(v128 v) noexcept { return vrev16q_u8(v); }
[[nodiscard]] inline v128 byteswap32(v128 v) noexcept { return vrev32q_u8(v); }
[[nodiscard]] inline v128 add16(v128 a, v128 b) noexcept {
    return vreinterpretq_u8_u16(vaddq_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
[[nodiscard]] inline v128 add_saturate_u16(v128 a, v128 b) noexcept {
    return vreinterpretq_u8_u16(vqaddq_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
[[nodiscard]] inline bool all_equal16(v128 a, v128 b) noexcept {
    return vminvq_u16(vceqq_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b))) == 0xFFFFu;
}

#else

using v128 = __m128i;

[[nodiscard]] inline v128 load(const void* p) noexcept { return _mm_loadu_si128(static_cast<const __m128i*>(p)); }
inline void store(void* p, v128 v) noexcept { _mm_storeu_si128(static_cast<__m128i*>(p), v); }
[[nodiscard]] inline v128 bytes(std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3, std::uint8_t b4,
                                std::uint8_t b5, std::uint8_t b6, std::uint8_t b7, std::uint8_t b8, std::uint8_t b9,
                                std::uint8_t b10, std::uint8_t b11, std::uint8_t b12, std::uint8_t b13,
                                std::uint8_t b14, std::uint8_t b15) noexcept {
    return _mm_setr_epi8(static_cast<char>(b0), static_cast<char>(b1), static_cast<char>(b2), static_cast<char>(b3),
                         static_cast<char>(b4), static_cast<char>(b5), static_cast<char>(b6), static_cast<char>(b7),
                         static_cast<char>(b8), static_cast<char>(b9), static_cast<char>(b10),
                         static_cast<char>(b11), static_cast<char>(b12), static_cast<char>(b13),
                         static_cast<char>(b14), static_cast<char>(b15));
}
[[nodiscard]] inline v128 splat16(std::uint16_t x) noexcept { return _mm_set1_epi16(static_cast<short>(x)); }
[[nodiscard]] inline v128 or_(v128 a, v128 b) noexcept { return _mm_or_si128(a, b); }
[[nodiscard]] inline v128 and_(v128 a, v128 b) noexcept { return _mm_and_si128(a, b); }
[[nodiscard]] inline v128 andnot(v128 a, v128 b) noexcept { return _mm_andnot_si128(a, b); }
template <int N> [[nodiscard]] inline v128 shl16(v128 v) noexcept { return _mm_slli_epi16(v, N); }
template <int N> [[nodiscard]] inline v128 shr16(v128 v) noexcept { return _mm_srli_epi16(v, N); }
template <int N> [[nodiscard]] inline v128 shl32(v128 v) noexcept { return _mm_slli_epi32(v, N); }
template <int N> [[nodiscard]] inline v128 shr32(v128 v) noexcept { return _mm_srli_epi32(v, N); }
[[nodiscard]] inline v128 sign_mask16(v128 v) noexcept { return _mm_cmplt_epi16(v, _mm_setzero_si128()); }
[[nodiscard]] inline v128 interleave_low16(v128 a, v128 b) noexcept { return _mm_unpacklo_epi16(a, b); }
[[nodiscard]] inline v128 interleave_high16(v128 a, v128 b) noexcept { return _mm_unpackhi_epi16(a, b); }
[[nodiscard]] inline v128 shuffle_bytes(v128 v, v128 indices) noexcept { return _mm_shuffle_epi8(v, indices); }
[[nodiscard]] inline v128 byteswap16(v128 v) noexcept {
    return _mm_shuffle_epi8(v, _mm_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
}
[[nodiscard]] inline v128 byteswap32(v128 v) noexcept {
    return _mm_shuffle_epi8(v, _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12));
}
[[nodiscard]] inline v128 add16(v128 a, v128 b) noexcept { return _mm_add_epi16(a, b); }
[[nodiscard]] inline v128 add_saturate_u16(v128 a, v128 b) noexcept { return _mm_adds_epu16(a, b); }
[[nodiscard]] inline bool all_equal16(v128 a, v128 b) noexcept {
    return _mm_movemask_epi8(_mm_cmpeq_epi16(a, b)) == 0xFFFF;
}

#endif

}  // namespace galaxy::simd
