#pragma once

#define LITTLEENDIAN 1

#if defined(_MSC_VER)
#define INLINE static __forceinline
#define THREAD_LOCAL __declspec(thread)
#else
#define INLINE static inline
#define THREAD_LOCAL _Thread_local
#endif

// Exact integer primitives on the shipped Windows x64 host. These require no
// optional CPU ISA or host floating-point rounding mode. The zero cases are
// explicit because bit-scan leaves its output index undefined for zero.
#if defined(_MSC_VER) && defined(_M_X64)
#include <stdint.h>
#include <intrin.h>
#include "primitiveTypes.h"

INLINE uint_fast8_t galaxy_softfloat_clz32(uint32_t value) {
    unsigned long index;
    return _BitScanReverse(&index, value) ? (uint_fast8_t)(31u - index) : 32u;
}
INLINE uint_fast8_t galaxy_softfloat_clz64(uint64_t value) {
    unsigned long index;
    return _BitScanReverse64(&index, value) ? (uint_fast8_t)(63u - index) : 64u;
}
#define softfloat_countLeadingZeros32 galaxy_softfloat_clz32
#define softfloat_countLeadingZeros64 galaxy_softfloat_clz64

#ifdef SOFTFLOAT_FAST_INT64
INLINE struct uint128 galaxy_softfloat_mul64To128(uint64_t a, uint64_t b) {
    struct uint128 result;
    result.v0 = _umul128(a, b, &result.v64);
    return result;
}
#define softfloat_mul64To128 galaxy_softfloat_mul64To128
#endif
#endif
