/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's draw.post service (docs/modding/services/draw.md): a mod's full-screen pixel programs over the frame.
   Import: LUMA_SERVICE(slot, "draw.post", "1.0"). No permission.

   A post program is cooked from the mod's assets/post/<name>.hlsl and post/<name>.json into <name>.post (LumaLink's
   `post` kind, materials 3.4 layer 4). The file defines `float4 post(PostIn i)`; it reads the frame so far with
   scene(uv) and scene_texel(pixel), and, when its json `reads` lists them, depth(uv) (the distance from the eye along the
   view axis, game units) and motion(uv) (where that surface was the frame before, minus uv). Its parameters are globals
   of their names, set from the stage's params (16 floats).

   Stages: WORLD, after the 3-D scene and the game's bloom, before 2-D; FRAME, after the HUD, before the present point.
   A mod has one program in each (its slot). In a stage the mods' programs run in Mods-list order, bottom first, each
   reading the one before. Every picture the host draws runs them, interpolated pictures too. */
#ifndef LUMA_DRAW_POST_H
#define LUMA_DRAW_POST_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_DRAW_POST_NAME "draw.post"
#define LUMA_DRAW_POST_MAJOR 1u
#define LUMA_DRAW_POST_MINOR 0u

/* Programs one mod holds at once. */
#define LUMA_DRAW_POST_PROGRAMS_MOST 8u
/* A stage's parameter floats (four rows of four). */
#define LUMA_DRAW_POST_PARAMS_MOST 16u

typedef enum LumaDrawPostStage {
    LUMA_DRAW_POST_WORLD = 0, /* after the 3-D scene and the game's bloom, before 2-D */
    LUMA_DRAW_POST_FRAME = 1  /* after the HUD (draw point hud.after), before the present point */
} LumaDrawPostStage;
#define LUMA_DRAW_POST_STAGES 2u

/* A program: the mod's until program_free or its end. */
typedef LumaHandle LumaDrawPostProgram;

typedef struct LumaDrawPostService {
    LumaServiceTable base;
    /* 1.0. Game thread. The mod's cooked post program `name` (<name>.post in its store), compiled by the host for this
       device. LUMA_OK and *out. A null or empty name or out: LUMA_E_ARGUMENT, reported; no such program: LUMA_E_NOT_FOUND,
       Draw.Post.Missing (540); the device's compiler refused it: LUMA_E_ARGUMENT, Draw.Post.Compile (541) with the
       compiler's first error; it reads motion and this frame has no motion vectors (temporal upscaling off):
       LUMA_E_ABSENT, Draw.Post.NoMotion (542); LUMA_DRAW_POST_PROGRAMS_MOST held: LUMA_E_LIMIT, reported. */
    int32_t (*program)(const LumaMod* mod, const char* name, LumaDrawPostProgram* out);
    /* 1.0. Game thread. The mod's slot in `stage` runs `program` from the next frame on (0: the slot empty). Its
       params are the program's defaults until `params`. LUMA_OK. A stage past FRAME: LUMA_E_ARGUMENT, reported; a
       program that is not the mod's: LUMA_E_HANDLE, reported. */
    int32_t (*set)(const LumaMod* mod, LumaDrawPostStage stage, LumaDrawPostProgram program);
    /* 1.0. Game thread. The slot's parameters from the next frame on: `count` floats (0 to LUMA_DRAW_POST_PARAMS_MOST)
       in the program's params order (four rows of four, a param never crossing a row); the rest keep theirs. LUMA_OK. A
       count past 16, a null values with a count, a value not finite: LUMA_E_ARGUMENT, reported; an empty slot:
       LUMA_E_STATE, Draw.Post.Empty (543). */
    int32_t (*params)(const LumaMod* mod, LumaDrawPostStage stage, const float* values, uint32_t count);
    /* 1.0. Game thread. The program freed (a slot running it empties). LUMA_OK; a program that is not the mod's:
       LUMA_E_HANDLE, reported. */
    int32_t (*program_free)(const LumaMod* mod, LumaDrawPostProgram program);
} LumaDrawPostService;

/* <name>.post in a mod's store, written by LumaLink's cook (materials 3.4 layer 4): little-endian, unaligned.
   LUMA_DRAW_POST_FILE_MAGIC, then u32 version (1), u32 reads (LUMA_DRAW_POST_READS_*), u32 param count, u32 text bytes,
   then each param (LUMA_DRAW_POST_PARAM_BYTES: char name[32] NUL-padded, u32 floats 1 to 4, u32 row 0 to 3, u32 column
   0 to 3, float default[4]), then the text (UTF-8, G's part of the program: the params mapped onto post_params and the
   mod's code; the host adds its prelude and entry). */
#define LUMA_DRAW_POST_FILE_MAGIC "NBPOST01"
#define LUMA_DRAW_POST_FILE_VERSION 1u
#define LUMA_DRAW_POST_HEADER_BYTES 24u
#define LUMA_DRAW_POST_PARAM_BYTES 60u
#define LUMA_DRAW_POST_READS_DEPTH 1u
#define LUMA_DRAW_POST_READS_MOTION 2u

#ifdef __cplusplus
}
#endif

#endif
