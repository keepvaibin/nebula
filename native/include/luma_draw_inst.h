/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's draw.inst service (docs/modding/services/draw.md): per-instance overrides on an actor's drawn model,
   seen on that one actor and on no other of its model. A colour over every material, a material's colour by name, a
   part hidden, a scale: the Gold Flower's gold Mario is this (a colour on Mario), not a feature. An override lives until
   release, the mod's end, the actor's death or its scene's end; then the model draws as the game made it again.
   Import: LUMA_SERVICE(slot, "draw.inst", "1.0").

   The actor's own model (the one its game code draws: MR::getModel), not its attached parts (content.model has its
   own). Applied each frame after the game's own animation of the model (colour animations, poses), before its draw. */
#ifndef LUMA_DRAW_INST_H
#define LUMA_DRAW_INST_H

#include <stdint.h>

#include "luma_content.h"
#include "luma_draw.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_DRAW_INST_NAME "draw.inst"
#define LUMA_DRAW_INST_MAJOR 1u
#define LUMA_DRAW_INST_MINOR 0u

/* Overrides one mod has at once, and all mods together. One actor has at most one of each mod. */
#define LUMA_DRAW_INSTS_MOST 64u
#define LUMA_DRAW_INSTS_ALL_MOST 512u
/* A scale's component: above 0, at most this. */
#define LUMA_DRAW_INST_SCALE_MOST 100.0f

/* An override of one actor's model: owned by the mod until release or its end. */
typedef LumaHandle LumaDrawInst;

/* set_material_param's names: a material's colours as its data has them (GX's): the material and ambient colours of
   channels 0 and 1, the four constant colours (0 to 1 each, RGBA), and the four colour registers (-4 to 4 each). */
#define LUMA_DRAW_INST_PARAM_MATERIAL0 "material0"
#define LUMA_DRAW_INST_PARAM_MATERIAL1 "material1"
#define LUMA_DRAW_INST_PARAM_AMBIENT0 "ambient0"
#define LUMA_DRAW_INST_PARAM_AMBIENT1 "ambient1"
/* "constant0" .. "constant3", "register0" .. "register3" alike. */

typedef struct LumaDrawInstService {
    LumaServiceTable base;
    /* 1.0. Game thread. The override of `actor`'s model for this mod: a new one, or the one the mod has on it already
       (the same handle). Nothing changes until a set_ call. LUMA_OK and *out. A null, dead or non-actor `actor`, one
       with no model, a null out: LUMA_E_ARGUMENT, reported; LUMA_DRAW_INSTS_MOST of the mod's or ALL_MOST of all:
       LUMA_E_LIMIT, reported. Off the game thread: LUMA_E_THREAD; while the game takes the mod out:
       LUMA_E_CANCELLED. */
    int32_t (*of)(const LumaMod* mod, LumaActor actor, LumaDrawInst* out);
    /* 1.0. Game thread. Every material's colours times `colour` (0xRRGGBBAA; 0xFFFFFFFF: as the game draws them):
       the material and ambient colours of both channels, the constant colours and the colour registers, alpha with
       them. LUMA_OK; an ended
       override or another mod's: LUMA_E_HANDLE, reported. Thread and taking out as of. */
    int32_t (*set_colour)(const LumaMod* mod, LumaDrawInst inst, LumaColor colour);
    /* 1.0. Game thread. The colour `name` (LUMA_DRAW_INST_PARAM_*) of the material named `material` (the model
       data's name; null: every material) set to `value` (4 floats, RGBA), in place of the game's, from the next
       frame; set_colour multiplies over it. A name not of the list, a value out of its range or not finite:
       LUMA_E_ARGUMENT, reported; no material of that name: LUMA_E_NOT_FOUND, reported. Handle as set_colour. */
    int32_t (*set_material_param)(const LumaMod* mod, LumaDrawInst inst, const char* material, const char* name,
                                  const float value[4]);
    /* 1.0. Game thread. Not in 1.0's game: a texture of the draw service on a material's map. LUMA_E_ABSENT after
       the handle's checks (the call is the contract's; a later 1.x fills it). */
    int32_t (*set_texture)(const LumaMod* mod, LumaDrawInst inst, const char* material, uint32_t map,
                           LumaDrawTexture texture);
    /* 1.0. Game thread. The joint named `part` and every joint under it shown (`on` 1) or hidden (0); null `part`:
       the whole model. Hidden by the mod is hidden whatever the game shows; shown gives the game its own choice back.
       No joint of that name: LUMA_E_NOT_FOUND, reported. Handle as set_colour. */
    int32_t (*set_visible)(const LumaMod* mod, LumaDrawInst inst, const char* part, int32_t on);
    /* 1.0. Game thread. The drawn model scaled by `scale` (3 floats, x y z of the actor's own axes, above 0 and at
       most LUMA_DRAW_INST_SCALE_MOST) about the actor's base, after the game's own scale; 1, 1, 1: as the game.
       Only the picture: the actor's collision and logic stay the game's. Out of range: LUMA_E_ARGUMENT, reported.
       Handle as set_colour. */
    int32_t (*set_scale)(const LumaMod* mod, LumaDrawInst inst, const float scale[3]);
    /* 1.0. Game thread. The override ended: the model draws as the game made it from the next frame. LUMA_OK; an
       ended override or another mod's: LUMA_E_HANDLE, reported. */
    int32_t (*release)(const LumaMod* mod, LumaDrawInst inst);
} LumaDrawInstService;

#ifdef __cplusplus
}
#endif

#endif
