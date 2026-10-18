#ifndef OSFASTCAST_H
#define OSFASTCAST_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TARGET_PC)
// The quantized paired-single conversions through GQR3 (u16, scale 0, set by OSInitFastCast): NaN and values at or
// past 65535 saturate to 65535, zero and negatives to 0, the rest truncate toward zero; u16 to f32 is exact.
static inline u16 __OSf32tou16(f32 in) {
    if (in != in || in >= 65535.0f) {
        return 0xFFFF;
    }
    return in > 0.0f ? (u16)in : (u16)0;
}

static inline void OSf32tou16(const f32* in, volatile u16* out) {
    *out = __OSf32tou16(*in);
}

static inline void OSu16tof32(const u16* in, volatile f32* out) {
    *out = (f32)*in;
}
#elif defined(__MWERKS__)
static inline u16 __OSf32tou16(register f32 in) {
    f32 a;
    register f32* ptr = &a;
    register u16 r;
    asm {
        psq_st in, 0(ptr), 1, 3
        lhz r, 0(ptr)
    }
    return r;
}

static inline void OSf32tou16(register f32* in, volatile register u16* out) {
    *out = __OSf32tou16(*in);
}
#else
#define OSf32tou16(in, out) asm volatile("psq_st   %1, 0(%0), 1, 3 " : : "b"(out), "f"(*(in)) : "memory")
#endif
#ifndef TARGET_PC
#define OSu16tof32(in, out) asm volatile("psq_l   %0, 0(%1), 1, 3  " : "=f"(*(out)) : "b"(in))
#endif

static inline void OSInitFastCast(void) {
#ifdef __MWERKS__
    asm
    {
        li      r3, 4
        oris    r3, r3, 4
        mtspr   0x392, r3

        li      r3, 5
        oris    r3, r3, 5
        mtspr   0x393, r3

        li      r3, 6
        oris    r3, r3, 6
        mtspr   0x394, r3

        li      r3, 7
        oris    r3, r3, 7
        mtspr   0x395, r3
    }
#endif
}

#ifdef __cplusplus
}
#endif

#endif  // OSFASTCAST_H
