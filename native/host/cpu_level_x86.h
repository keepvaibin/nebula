// SPDX-License-Identifier: GPL-3.0-only
// Whether this CPU runs code built for x86-64-v3: AVX, AVX2, FMA3, F16C, MOVBE, BMI1, BMI2, LZCNT, with the OS saving
// the AVX registers. Static: the AVX2 host's entry point (host_entry_win32.cpp, built without AVX) and host_main.cpp
// (built for the host's level) each keep their own copy, so the linker never gives the entry point an AVX2 one.
#pragma once

#if defined(_WIN32) && defined(_M_X64)
#include <intrin.h>

#if defined(__clang__)
__attribute__((target("xsave")))
#endif
static bool cpu_runs_x86_64_v3() {
    int registers[4]{};
    __cpuid(registers, 0);
    if (registers[0] < 7) return false;
    __cpuid(registers, 1);
    constexpr int kLeaf1 = (1 << 12) | (1 << 22) | (1 << 27) | (1 << 28) | (1 << 29);
    if ((registers[2] & kLeaf1) != kLeaf1 || (_xgetbv(0) & 0x6u) != 0x6u) return false;
    __cpuidex(registers, 7, 0);
    constexpr int kLeaf7 = (1 << 3) | (1 << 5) | (1 << 8);
    if ((registers[1] & kLeaf7) != kLeaf7) return false;
    __cpuid(registers, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(registers[0]) < 0x80000001u) return false;
    __cpuid(registers, static_cast<int>(0x80000001u));
    return (registers[2] & (1 << 5)) != 0;
}
#endif
