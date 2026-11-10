/* SPDX-License-Identifier: GPL-3.0-only */
/* <uchar.h> for C units on Apple's SDKs, which have none (nebula_prelude.h includes it): the two character types, as
   C11 names them. C++ has them as keywords, and libc++'s own <uchar.h> comes before this folder. */
#ifndef NEBULA_APPLE_UCHAR_H
#define NEBULA_APPLE_UCHAR_H

#if defined(__has_include_next)
#if __has_include_next(<uchar.h>)
#include_next <uchar.h>
#define NEBULA_APPLE_UCHAR_FROM_SDK 1
#endif
#endif

#if !defined(NEBULA_APPLE_UCHAR_FROM_SDK) && !defined(__cplusplus)
#include <stdint.h>
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#endif

#endif
