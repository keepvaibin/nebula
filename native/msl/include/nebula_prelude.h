/* Force-included first in every unit of the game module (Petari's code, built natively).

   The console game linked its own C library, MSL. The game module keeps MSL's interface: the host C library where
   the two agree, msl_runtime.c where they do not (16-bit wchar_t strings and MSL's formatters, MSL's
   floating-point class codes, "C"-locale case-insensitive compares, abs taking int only). The host headers are
   included here, before any game header, so the renames below never reach their declarations. */
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
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nebula_endian.h"

/* The game's types.h defines nullptr as 0 (the code was written for MSL's macro); clang's stddef.h would declare
   std::nullptr_t again under that macro on a later include. The host declared it above. */
#ifndef _NULLPTR_T
#define _NULLPTR_T
#endif

#if __SIZEOF_WCHAR_T__ != 2
#error "the game module needs a 16-bit wchar_t (-fshort-wchar): disc text and the game's wide strings are UTF-16"
#endif

#ifdef __cplusplus
extern "C" {
#endif

size_t nebula_msl_wcslen(const wchar_t* s);
int nebula_msl_wcscmp(const wchar_t* a, const wchar_t* b);
wchar_t* nebula_msl_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* nebula_msl_wcschr(const wchar_t* s, wchar_t c);
int nebula_msl_swprintf(wchar_t* s, size_t n, const wchar_t* format, ...);
int nebula_msl_vswprintf(wchar_t* s, size_t n, const wchar_t* format, va_list arg);
int nebula_msl_sprintf(char* s, const char* format, ...);
int nebula_msl_snprintf(char* s, size_t n, const char* format, ...);
int nebula_msl_vsprintf(char* s, const char* format, va_list arg);
int nebula_msl_vsnprintf(char* s, size_t n, const char* format, va_list arg);
int nebula_msl_fpclassifyf(float x);
int nebula_msl_fpclassifyd(double x);

#ifdef __cplusplus
}
#endif

/* MSL's abs(int) is the only abs: a float argument converts to int, as on the console. INT_MIN maps to itself. */
static inline int nebula_msl_abs(int x) {
    return (int)(x < 0 ? 0u - (unsigned int)x : (unsigned int)x);
}

/* MSL's stricmp: "C"-locale lower case, signed char comparison, result -1, 0 or 1. */
static inline int nebula_msl_stricmp(const char* s1, const char* s2) {
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

#define abs nebula_msl_abs
#define stricmp nebula_msl_stricmp
#define strcasecmp nebula_msl_stricmp
#define wcslen nebula_msl_wcslen
#define wcscmp nebula_msl_wcscmp
#define wcsncpy nebula_msl_wcsncpy
#define wcschr nebula_msl_wcschr
#define swprintf nebula_msl_swprintf
#define vswprintf nebula_msl_vswprintf
/* MSL's narrow formatter too: a null string prints as nothing (the host's prints "(null)"), and long is 32 bits. */
#define sprintf nebula_msl_sprintf
#define snprintf nebula_msl_snprintf
#define vsprintf nebula_msl_vsprintf
#define vsnprintf nebula_msl_vsnprintf
#define __fpclassifyf nebula_msl_fpclassifyf
#define __fpclassifyd nebula_msl_fpclassifyd

/* PowerPC's divw: no trap; a zero divisor (or 0x80000000 / -1) gives 0, or -1 for a negative dividend. For sites that
   divide by zero every frame, where a trap (os/divide_native.c finishes it with this result) would cost time. */
static inline int __nebula_divw(int a, int b) {
    return b == 0 || (a == (int)0x80000000 && b == -1) ? (a < 0 ? -1 : 0) : a / b;
}
/* PowerPC's divwu: a zero divisor gives 0. */
static inline unsigned __nebula_divwu(unsigned a, unsigned b) {
    return b == 0 ? 0 : a / b;
}

#endif
