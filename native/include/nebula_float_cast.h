/* Float to integer with saturation: NaN and values past the top give the top, values at or past the bottom the bottom, the
   rest truncate toward zero. */
#ifndef NEBULA_FLOAT_CAST_H
#define NEBULA_FLOAT_CAST_H

#include <stdint.h>

static inline uint8_t nebula_f32_to_u8(float value) {
    if (value != value || value >= 255.0f) {
        return 255;
    }
    return value > 0.0f ? (uint8_t)value : (uint8_t)0;
}

static inline int16_t nebula_f32_to_s16(float value) {
    if (value != value || value >= 32767.0f) {
        return 32767;
    }
    return value <= -32768.0f ? (int16_t)-32768 : (int16_t)value;
}

/* As the console's conversion (fctiwz) gives: NaN the bottom; the rest is the plain cast, which the game module's
   -fno-strict-float-cast-overflow saturates as the console does. */
static inline int32_t nebula_f32_to_s32_console(float value) {
    return value != value ? INT32_MIN : (int32_t)value;
}

#endif /* NEBULA_FLOAT_CAST_H */
