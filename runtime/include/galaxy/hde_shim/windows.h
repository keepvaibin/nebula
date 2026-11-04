// SPDX-License-Identifier: GPL-3.0-only
// MinHook's instruction decoder asks for this header only for its fixed-width types. Linux and Mac have them under other names.
#pragma once
#include <stdint.h>
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
