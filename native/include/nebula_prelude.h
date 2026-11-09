/* Force-included first in every unit of the game module. */
#ifndef NEBULA_PRELUDE_H
#define NEBULA_PRELUDE_H

/* Code that manages memory itself (the game's heaps), left uninstrumented in AddressSanitizer test builds. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NEBULA_NO_ASAN __attribute__((no_sanitize("address")))
#endif
#endif
#ifndef NEBULA_NO_ASAN
#define NEBULA_NO_ASAN
#endif

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <cmath>
#else
#include <math.h>
#include <uchar.h>
#endif

/* The game's constants, as floats. */
#undef M_PI
#define M_PI 3.14159265358979323846f
#define DEG_TO_RAD(degrees) (degrees * (M_PI / 180.0f))

/* "C"-locale case-insensitive compare: -1, 0 or 1. */
static inline int nebula_stricmp(const char* s1, const char* s2) {
    for (;;) {
        signed char c1 = (signed char)*s1++;
        signed char c2 = (signed char)*s2++;
        if (c1 >= 'A' && c1 <= 'Z') {
            c1 = (signed char)(c1 + ('a' - 'A'));
        }
        if (c2 >= 'A' && c2 <= 'Z') {
            c2 = (signed char)(c2 + ('a' - 'A'));
        }
        if (c1 < c2) {
            return -1;
        }
        if (c1 > c2) {
            return 1;
        }
        if (c1 == 0) {
            return 0;
        }
    }
}

#include "nebula_types.h"
/* A fatal error of the game's: the host's crash report names the file, the line and the message. Does not return. */
#ifdef __cplusplus
extern "C"
#endif
__attribute__((noreturn)) __attribute__((format(printf, 3, 4))) void nebula_panic(const char* file, int line, const char* format, ...);
#define NEBULA_PANIC(...) nebula_panic(__FILE__, __LINE__, __VA_ARGS__)

#include "nebula_float_cast.h"
#include "nebula_format.h"

#endif
