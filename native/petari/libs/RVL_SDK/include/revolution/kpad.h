#ifndef KPAD_H
#define KPAD_H

#include "revolution/mtx.h"
#include "revolution/types.h"
#include "revolution/wpad.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vec2 {
    f32 x;
    f32 y;
} Vec2;

/* The HOME Menu's controller record names a status it does not read (HBMKPadData). */
typedef struct KPADStatus KPADStatus;

#ifdef __cplusplus
}
#endif

#endif  // KPAD_H
