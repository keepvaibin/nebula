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

#define ROUND_UP(x, align) (((x) + (align) - 1) & (-(align)))

#define ARRAY_SIZE(o) (s32)(sizeof(o) / sizeof(o[0]))
#define ARRAY_SIZEU(o) (sizeof(o) / sizeof(o[0]))

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#endif  // TYPES_H
