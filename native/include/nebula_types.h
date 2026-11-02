#ifndef TYPES_H
#define TYPES_H

// The same fundamental types on every host, so overloads resolve alike everywhere. `long` is never one of them.
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

typedef volatile u32 vu32;
typedef volatile s32 vs32;

typedef float f32;
typedef double f64;

typedef int BOOL;


#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))

#define ATTRIBUTE_PACKED __attribute__((packed))

// Natively the compiler checks the game's printf-style calls (an audit build's -Wformat).
#define ATTRIBUTE_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))

// Data defined in headers is an inline variable.
#ifdef __cplusplus
#define ATTRIBUTE_WEAK_DEF(...) inline __VA_ARGS__
#else
#define ATTRIBUTE_WEAK_DEF(...) __VA_ARGS__
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef ASSERT
#define ASSERT(exp) ((void)0)
#endif

#ifndef ASSERTMSG
#define ASSERTMSG(exp, msg) ((void)0)
#endif

#define ROUND_UP(x, align) (((x) + (align) - 1) & (-(align)))
#define ROUND_UP_PTR(x, align) ((void*)((((uintptr_t)(x)) + (align) - 1) & (~((uintptr_t)(align) - 1))))

// The mask in X's (promoted) type: with a u32 N, ~(N - 1) would clear the upper half of a 64-bit X.
#define ALIGN_PREV(X, N) ((X) & ~((__typeof__((X) + 0))(N) - 1))
#define ALIGN_NEXT(X, N) ALIGN_PREV(((X) + (N) - 1), N)

#define ARRAY_SIZE(o) (s32)(sizeof(o) / sizeof(o[0]))
#define ARRAY_SIZEU(o) (sizeof(o) / sizeof(o[0]))

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define IS_ALIGNED(x, align) (((unsigned long)(x) & ((align) - 1)) == 0)
#define IS_NOT_ALIGNED(X, N) (((X) & ((N) - 1)) != 0)

// Comparing a non-volatile reference type to NULL is tautological
// and triggers a warning on modern compilers, but in some cases is
// required to match the original assembly.
#define IS_REF_NULL(r) (0)
#define IS_REF_NONNULL(r) (1)

#endif  // TYPES_H
