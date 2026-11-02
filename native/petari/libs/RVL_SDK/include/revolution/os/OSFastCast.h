#ifndef OSFASTCAST_H
#define OSFASTCAST_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

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

static inline void OSInitFastCast(void) {
}

#ifdef __cplusplus
}
#endif

#endif  // OSFASTCAST_H
