/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's content.model service (plan 3.2; contract models, section 7): a model drawn with an actor of the game,
   following its base, one of its joints, or its whole skeleton by joint name; shown, hidden, detached; the player's
   parts listed. Import: LUMA_SERVICE(slot, "content.model", "1.0"). */
#ifndef LUMA_CONTENT_MODEL_H
#define LUMA_CONTENT_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "luma_content.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_CONTENT_MODEL_NAME "content.model"
#define LUMA_CONTENT_MODEL_MAJOR 1u
#define LUMA_CONTENT_MODEL_MINOR 1u

/* Models one mod has attached and not yet detached, at once. */
#define LUMA_CONTENT_MODEL_MOST 64u

/* A model a mod attached: owned by the mod, ended by detach, by its actor's death, by its scene's end, or by the
   mod's. */
typedef LumaHandle LumaContentModel;

/* What an attached model follows. */
typedef enum LumaContentModelFollow {
    LUMA_CONTENT_MODEL_ACTOR = 0,    /* the actor's base (its position, rotation and scale) */
    LUMA_CONTENT_MODEL_JOINT = 1,    /* one joint (or socket) of the actor, named by `joint` */
    LUMA_CONTENT_MODEL_SKELETON = 2  /* each joint of the model takes the actor's joint of its name, every frame; a
                                        joint with no such name on the actor moves with its own parent */
} LumaContentModelFollow;

/* One of the player's parts: a model of its own the game draws at a joint of the body. */
typedef struct LumaPlayerPart {
    uint32_t size;     /* sizeof(LumaPlayerPart) as the mod was built */
    const char* name;  /* "MarioHandR": the model, ObjectData/<name>.arc */
    const char* joint; /* the body's joint it is drawn at ("HandR"); "" for one drawn at the base */
} LumaPlayerPart;      /* 1.0: 24 bytes on 64-bit */

typedef struct LumaContentModelService {
    LumaServiceTable base;
    /* 1.0. Game thread. The model `model` drawn with `actor`, following as `follow` says: the joint `joint` (null: the
       actor's base) for LUMA_CONTENT_MODEL_JOINT, offset and turned by `place` (null: none; ignored by
       LUMA_CONTENT_MODEL_SKELETON). It is drawn as the game draws its own models (the game's lights, shadows and
       draw order), hidden while the actor is hidden or clipped. LUMA_OK and *out. A model in no store: the placeholder
       model, LUMA_OK, and Content.MissingModel in the mod log; a joint the actor's model lacks: its base, LUMA_OK, and
       Content.MissingJoint. A null or dead actor, an actor that is not one or has no model, an empty name, a follow
       outside the enum, a null out: LUMA_E_ARGUMENT, reported; a place below 1.0's size: LUMA_E_ARGUMENT, reported
       (Service.StructSize); LUMA_CONTENT_MODEL_MOST attached: LUMA_E_LIMIT, reported. Off the game thread:
       LUMA_E_THREAD, reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*attach)(const LumaMod* mod, LumaActor actor, LumaModelName model, LumaContentModelFollow follow,
                      const char* joint, const LumaPlace* place, LumaContentModel* out);
    /* 1.0. Game thread. Stops drawing it; the handle ends. LUMA_OK; an ended handle: LUMA_E_HANDLE, reported; off the
       game thread: LUMA_E_THREAD, reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*detach)(const LumaMod* mod, LumaContentModel model);
    /* 1.0. Game thread. Shown (`on` true) or hidden (false); hidden with its actor as well. LUMA_OK; an ended handle:
       LUMA_E_HANDLE, reported; off the game thread: LUMA_E_THREAD, reported; while the game takes the mod out:
       LUMA_E_CANCELLED. */
    int32_t (*show)(const LumaMod* mod, LumaContentModel model, bool on);
    /* 1.0. Any thread. The player's parts, the first `count` into `parts` (each one's size set by the mod; null when
       count is 0), and their number in *total (null: not wanted). LUMA_OK; a part's size below 1.0's, or a null parts
       with a count: LUMA_E_ARGUMENT, reported (Service.StructSize). */
    int32_t (*player_parts)(const LumaMod* mod, LumaPlayerPart* parts, uint32_t count, uint32_t* total);
    /* 1.1. Game thread. A parameter of a material shader (plan 3.4, contract materials 9): the attached model's material
       `material` takes `rgba` (four floats; a float parameter reads the first) as its parameter `param`, on this model
       only, until it is set again or a clip that moves that colour plays. The cook names each parameter's place in
       <model>.params beside the model: places 0 to 2 hold -4 to 4, places 3 to 6 hold 0 to 1, both in 255ths; a value
       outside is held to them. LUMA_OK. A material or parameter the model's parameters lack, or a model that shows
       nothing: LUMA_E_NOT_FOUND, Content.MissingParam in the mod log. An ended handle: LUMA_E_HANDLE, reported; a null
       material, param or rgba, or a value that is not a number: LUMA_E_ARGUMENT, reported. Off the game thread:
       LUMA_E_THREAD, reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*set_param)(const LumaMod* mod, LumaContentModel model, const char* material, const char* param,
                         const float* rgba);
} LumaContentModelService;

#ifdef __cplusplus
}
#endif

#endif
