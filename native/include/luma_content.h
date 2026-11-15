/* SPDX-License-Identifier: GPL-3.0-only */
/* What the content services share (content/models.md, effects.md): an actor of the game as a mod holds it, a place on
   it, and the name kinds the validator lifts and checks (references 5). */
#ifndef LUMA_CONTENT_H
#define LUMA_CONTENT_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An actor of the game (a LiveActor or a class deriving from it), as the game gave it to the mod: a hook's `this`, a
   field read, a call's result. Valid while its scene lasts; a service checks its class before using it. */
typedef void* LumaActor;

/* Names of a kind (references 5): each a `const char*` the validator lifts from the mod's code and checks against
   the mod's content and the game's names before any run. */
typedef const char* LumaEffectName;
typedef const char* LumaModelName;

/* A place on a joint: an offset in game units and a turn in degrees (X, Y, Z, composed as the game composes a joint's:
   MR::makeMtxRotate), both in the joint's space. */
typedef struct LumaPlace {
    uint32_t size; /* sizeof(LumaPlace) as the mod was built */
    float offset[3];
    float turn[3];
} LumaPlace; /* 1.0: 28 bytes */

#ifdef __cplusplus
}
#endif

#endif
