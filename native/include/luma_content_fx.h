/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's content.fx service (contract effects, section 3): a mod's particle effects, or the game's, started on an
   actor or a model the mod attached, stopped and tinted. Import: LUMA_SERVICE(slot, "content.fx", "1.0"). */
#ifndef LUMA_CONTENT_FX_H
#define LUMA_CONTENT_FX_H

#include <stdint.h>

#include "luma_content.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_CONTENT_FX_NAME "content.fx"
#define LUMA_CONTENT_FX_MAJOR 1u
#define LUMA_CONTENT_FX_MINOR 0u

/* Effects one mod has started and not yet ended, at once. */
#define LUMA_CONTENT_FX_MOST 256u

typedef struct LumaContentFxService {
    LumaServiceTable base;
    /* 1.0. Game thread. The effect `name` on `actor`: at the joint `joint` (null: the actor's base), offset and turned
       by `place` (null: none), followed each frame (`follow` 1) or left where it started (0). LUMA_OK and *out, a handle
       owned by the mod. A name in no store: the placeholder effect, LUMA_OK, and Content.MissingEffect in the mod log;
       a joint the actor's model lacks: the base, LUMA_OK, and Content.MissingJoint. A null or dead actor, an actor that
       is not one, an empty name, a null out: LUMA_E_ARGUMENT, reported; a place below 1.0's size: LUMA_E_ARGUMENT,
       reported (Service.StructSize); LUMA_CONTENT_FX_MOST open: LUMA_E_LIMIT, reported. */
    int32_t (*start)(const LumaMod* mod, LumaActor actor, LumaEffectName name, const char* joint, const LumaPlace* place,
                     int32_t follow, LumaHandle* out);
    /* 1.0. Game thread. As start, on a model the mod attached (models 7) at its joint or socket `socket`. An ended model
       handle: LUMA_E_HANDLE, reported; an unknown socket: Content.MissingJoint, the effect at the model's root. */
    int32_t (*start_on_model)(const LumaMod* mod, LumaHandle model, LumaEffectName name, const char* socket,
                              const LumaPlace* place, int32_t follow, LumaHandle* out);
    /* 1.0. Game thread. Stops making particles: those alive fade as the effect says (`now` 0), or all go at once (1).
       The handle ends. An ended handle: LUMA_E_HANDLE, reported. */
    int32_t (*stop)(const LumaMod* mod, LumaHandle effect, int32_t now);
    /* 1.0. Game thread. Scale and colour on top of the effect's own (1 and 0xFFFFFFFF: none; colour as 0xRRGGBBAA), as
       the game's setEffectBaseScale and setEffectPrmColor. A scale not above 0: LUMA_E_ARGUMENT, reported. */
    int32_t (*tint)(const LumaMod* mod, LumaHandle effect, float scale, uint32_t rgba);
} LumaContentFxService;

#ifdef __cplusplus
}
#endif

#endif
