#ifndef NEBULA_COLOR_H
#define NEBULA_COLOR_H

/* The game's colour values: 8 bits a channel (red, green, blue, alpha), and the signed 16-bit form some shader
   constants take (each channel -1024..1023 for -4..4 in 255ths). */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NebulaColor8 {
    uint8_t r, g, b, a;
} NebulaColor8;

typedef struct NebulaColor16 {
    int16_t r, g, b, a;
} NebulaColor16;

#ifdef __cplusplus
}
#endif

#endif
