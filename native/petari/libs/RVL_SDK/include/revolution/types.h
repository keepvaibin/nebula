#ifndef TYPES_H
#define TYPES_H

#ifdef TARGET_PC
// Native builds: the console's widths as the same fundamental types on every host, so overloads resolve alike
// everywhere. `long` is never one of them (64-bit on LP64 hosts).
#include <stddef.h>
#include <stdint.h>
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef char NebulaTypesCheck[(sizeof(s16) == 2 && sizeof(s32) == 4 && sizeof(s64) == 8) ? 1 : -1];

#ifdef __cplusplus
extern "C++" {
// The console's s32 and u32 are long and unsigned long, so the game's long literals (0L, 1UL) match its s32 and
// u32 overloads exactly. NebulaLongCall<R, A, B, C>::Type is R when every argument is an integer and one is a long;
// the overloads that use it forward such calls to the s32/u32 version, as the console resolved them.
template < typename T >
struct NebulaInt {
    enum { integer = 0, wide = 0 };
};
#define NEBULA_INT(T, W)                                                                                                                             \
    template <>                                                                                                                                      \
    struct NebulaInt< T > {                                                                                                                          \
        enum { integer = 1, wide = W };                                                                                                              \
    };
NEBULA_INT(char, 0)
NEBULA_INT(signed char, 0)
NEBULA_INT(unsigned char, 0)
NEBULA_INT(short, 0)
NEBULA_INT(unsigned short, 0)
NEBULA_INT(int, 0)
NEBULA_INT(unsigned int, 0)
NEBULA_INT(long long, 0)
NEBULA_INT(unsigned long long, 0)
NEBULA_INT(long, 1)
NEBULA_INT(unsigned long, 1)
#undef NEBULA_INT

template < bool B, typename R >
struct NebulaEnableIf {};
template < typename R >
struct NebulaEnableIf< true, R > {
    typedef R Type;
};

template < typename R, typename A, typename B = int, typename C = int >
struct NebulaLongCall
    : NebulaEnableIf< NebulaInt< A >::integer && NebulaInt< B >::integer && NebulaInt< C >::integer &&
                          (NebulaInt< A >::wide || NebulaInt< B >::wide || NebulaInt< C >::wide),
                      R > {};
}
#endif

// Fields whose layout a file or the hardware defines (Nebula's nebula_be.hpp): big-endian natively, the plain types
// for CodeWarrior. C units of the native build get neither, so a converted header used from C does not compile.
#if defined(TARGET_PC) && defined(__cplusplus)
extern "C++" {
#include <nebula_be.hpp>
}
#elif !defined(TARGET_PC)
#define NEBULA_BE(T) T
#define NEBULA_OFFSET(T) T*
#endif
#else
typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
#endif

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float f32;
typedef double f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;

typedef int BOOL;

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

// Native builds keep nullptr as 0, as the game was written for; `override` is the C++11 keyword there.
#ifndef nullptr
#ifdef __cplusplus
#define nullptr 0
#endif
#endif

#ifndef TARGET_PC
#ifndef override
#ifdef __cplusplus
#define override
#endif
#endif
#endif

#ifdef __MWERKS__
#define __REGISTER register
#else
#define __REGISTER
#endif

#if !defined(AT_ADDRESS)
#if defined(__MWERKS__)
#define AT_ADDRESS(x)							: x
#else
#define AT_ADDRESS(x)
#endif
#endif

#if __MWERKS__
#if !defined(METRO_TRK) && __MWERKS__ >= 0x3000
#define ALWAYS_INLINE __attribute__((always_inline))
#define NO_INLINE __attribute__((noinline))
#else
#define ALWAYS_INLINE
#define NO_INLINE
#endif
#else
#define ALWAYS_INLINE
#define NO_INLINE
#endif

// Natively too: the GPU addresses textures and display lists in 32-byte units, and the layouts must stay the console's.
#if __MWERKS__ || defined(TARGET_PC)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#else
#define ATTRIBUTE_ALIGN(num)
#endif

#if __MWERKS__ || defined(TARGET_PC)
#define ATTRIBUTE_PACKED __attribute__((packed))
#else
#define ATTRIBUTE_PACKED
#endif

// Natively a function the game replaces (Game/System/Overwrite.cpp) has its original left out of the build, so a missing
// or doubled definition fails the link instead of the linker picking one; data defined in headers is an inline variable
// (ATTRIBUTE_WEAK_DEF).
#if __MWERKS__
#define ATTRIBUTE_WEAK __attribute__((weak))
#else
#define ATTRIBUTE_WEAK
#endif

// Natively the compiler checks the game's printf-style calls (an audit build's -Wformat).
#if defined(TARGET_PC)
#define ATTRIBUTE_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#else
#define ATTRIBUTE_PRINTF(fmt, first)
#endif

#if defined(TARGET_PC) && defined(__cplusplus)
#define ATTRIBUTE_WEAK_DEF(...) inline __VA_ARGS__
#else
#define ATTRIBUTE_WEAK_DEF(...) __VA_ARGS__ ATTRIBUTE_WEAK
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define ROUND_UP(x, align) (((x) + (align) - 1) & (-(align)))
#ifdef TARGET_PC
#define ROUND_UP_PTR(x, align) ((void*)((((uintptr_t)(x)) + (align) - 1) & (~((uintptr_t)(align) - 1))))
#else
#define ROUND_UP_PTR(x, align) ((void*)((((u32)(x)) + (align) - 1) & (~((align) - 1))))
#endif

#ifdef TARGET_PC
// The mask in X's (promoted) type: with a u32 N, ~(N - 1) would clear the upper half of a 64-bit X.
#define ALIGN_PREV(X, N) ((X) & ~((__typeof__((X) + 0))(N) - 1))
#else
#define ALIGN_PREV(X, N) ((X) & ~((N) - 1))
#endif
#define ALIGN_NEXT(X, N) ALIGN_PREV(((X) + (N) - 1), N)

#define ARRAY_SIZE(o) (s32)(sizeof(o) / sizeof(o[0]))
#define ARRAY_SIZEU(o) (sizeof(o) / sizeof(o[0]))

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define FOURCC(c0, c1, c2, c3) (u32)((c0 & 0xFF) << 24 | (c1 & 0xFF) << 16 | (c2 & 0xFF) << 8 | (c3 & 0xFF))

#define IS_ALIGNED(x, align) (((unsigned long)(x) & ((align) - 1)) == 0)
#define IS_NOT_ALIGNED(X, N) (((X) & ((N) - 1)) != 0)

// Comparing a non-volatile reference type to NULL is tautological
// and triggers a warning on modern compilers, but in some cases is
// required to match the original assembly.
#if defined(__MWERKS__) || defined(DECOMPCTX)
#define IS_REF_NULL(r) (&(r) == NULL)
#define IS_REF_NONNULL(r) (&(r) != NULL)
#else
#define IS_REF_NULL(r) (0)
#define IS_REF_NONNULL(r) (1)
#endif

/* just some common intrinsics */

#if defined(TARGET_PC)
// The game follows the reciprocal square root estimate with a Newton step wherever it uses it, so natively it is the
// host's estimate (docs/NATIVE_PERFORMANCE.md: half the latency of 1/sqrtf, the same accuracy after the step): SSE's
// 12-bit rsqrtss, or AArch64's 8-bit frsqrte with one step of its own to reach the console's precision.
#include <math.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
static inline f32 __frsqrte(f32 x) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x))); }
#elif defined(__aarch64__)
#include <arm_neon.h>
static inline f32 __frsqrte(f32 x) {
    const f32 e = vrsqrtes_f32(x);
    return e * vrsqrtss_f32(x * e, e);
}
#else
static inline f32 __frsqrte(f32 x) { return 1.0f / sqrtf(x); }
#endif
static inline u32 __cntlzw(u32 x) { return x != 0u ? (u32)__builtin_clz(x) : 32u; }
static inline s32 __abs(s32 x) { return x < 0 ? -x : x; }
// glibc declares its own __fabsf and __fabs (math.h, included above): the console's intrinsics take other names.
#define __fabsf __nebula_fabsf
#define __fabs __nebula_fabs
static inline f32 __fabsf(f32 x) { return fabsf(x); }
static inline f64 __fabs(f64 x) { return fabs(x); }
static inline void* __memcpy(void* d, const void* s, int n) { return memcpy(d, s, (size_t)n); }
#elif !defined(__MWERKS__)
f32 __frsqrte(f32);
u32 __cntlzw(u32);
s32 __abs(s32);
f32 __fabsf(f32);
f64 __fabs(f64);
void* __memcpy(void*, const void*, int);
#endif

#endif  // TYPES_H
