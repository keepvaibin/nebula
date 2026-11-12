/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's draw.light service (plan 2.4 layer 6; contract draw): a mod's own lights in the world. A light is a
   glowing body at a place, of a colour and an intensity in candela. Traced (ray tracing on), it is an emitter like
   the game's lamps: it lights what it sees, casts shadows where `casts` says, and is seen in reflections. Not traced,
   it joins the game's own light set as the game's lamps do (a Luma's glow, a fireball's): the game lights with one
   such lamp at a time, the one nearest the player, blending to it over half a second. A light lives until remove or
   the mod's end; it is in whatever scene runs, and in none while the game loads one. Import:
   LUMA_SERVICE(slot, "draw.light", "1.0").

   Units: the game's world units (centimetres) and axes; colour 0 to 1 as the game's own lights give it (sRGB). */
#ifndef LUMA_DRAW_LIGHT_H
#define LUMA_DRAW_LIGHT_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_DRAW_LIGHT_NAME "draw.light"
#define LUMA_DRAW_LIGHT_MAJOR 1u
#define LUMA_DRAW_LIGHT_MINOR 0u

/* Lights one mod has at once, and all mods together. */
#define LUMA_DRAW_LIGHTS_MOST 16u
#define LUMA_DRAW_LIGHTS_ALL_MOST 256u
/* A light's intensity, candela: above 0, at most this (a bright lamp at 1 m; the sun is no draw.light). */
#define LUMA_DRAW_LIGHT_CANDELA_MOST 5000.0f
/* A light's body, world units: 0 takes LUMA_DRAW_LIGHT_RADIUS_DEFAULT; else from LEAST to MOST. */
#define LUMA_DRAW_LIGHT_RADIUS_DEFAULT 25.0f
#define LUMA_DRAW_LIGHT_RADIUS_LEAST 5.0f
#define LUMA_DRAW_LIGHT_RADIUS_MOST 400.0f
/* A light's centre: each coordinate within this of the world's origin (the game's worlds lie well inside it). */
#define LUMA_DRAW_LIGHT_FAR 10000000.0f

/* LumaDrawLightDesc.flags. */
enum {
    LUMA_DRAW_LIGHT_CASTS = 1u << 0 /* traced: what it lights is shadowed by what stands between (else it lights
                                       through); not traced the game's lamp casts none either way */
};

/* A light: owned by the mod until remove or its end. */
typedef LumaHandle LumaDrawLight;

typedef struct LumaDrawLightDesc {
    uint32_t size;     /* sizeof(LumaDrawLightDesc) as the mod was built */
    float position[3]; /* its centre in the world: each within LUMA_DRAW_LIGHT_FAR */
    float colour[3];   /* 0 to 1 each (sRGB, as the game's lights); all 0 is dark */
    float candela;     /* its intensity: above 0, at most LUMA_DRAW_LIGHT_CANDELA_MOST */
    float radius;      /* its glowing body's radius: 0 for the default, else LEAST to MOST */
    uint32_t flags;    /* LUMA_DRAW_LIGHT_* */
} LumaDrawLightDesc;

typedef struct LumaDrawLightService {
    LumaServiceTable base;
    /* 1.0. Game thread. A light as `desc` says, lit from the next frame the game draws. LUMA_OK and *out. A null desc
       or out, a value outside its range or not finite (the argument named: position, colour, candela, radius,
       flags: a bit this version does not know): LUMA_E_ARGUMENT, reported; a desc's size under
       sizeof(LumaDrawLightDesc) 1.0: LUMA_E_ARGUMENT, Service.StructSize in the mod log (a larger one, a later
       version's, is read up to 1.0's fields); LUMA_DRAW_LIGHTS_MOST of the mod's, or LUMA_DRAW_LIGHTS_ALL_MOST of all
       mods': LUMA_E_LIMIT, reported. Off the game thread: LUMA_E_THREAD, reported; while the game takes the mod out:
       LUMA_E_CANCELLED. */
    int32_t (*add)(const LumaMod* mod, const LumaDrawLightDesc* desc, LumaDrawLight* out);
    /* 1.0. Game thread. The light's centre to `position` (3 floats, each within LUMA_DRAW_LIGHT_FAR), from the next
       frame drawn. LUMA_OK; an ended light or another mod's: LUMA_E_HANDLE, reported; null, not finite or farther:
       LUMA_E_ARGUMENT, reported. Thread and taking out as add. */
    int32_t (*move)(const LumaMod* mod, LumaDrawLight light, const float position[3]);
    /* 1.0. Game thread. Everything of the light as `desc` says (its handle stays). Answers as add, and as move for
       the handle. */
    int32_t (*set)(const LumaMod* mod, LumaDrawLight light, const LumaDrawLightDesc* desc);
    /* 1.0. Game thread. The light is gone from the next frame drawn (not traced, the game's lamp fades out from it as
       from any lamp that goes). LUMA_OK; an ended light or another mod's: LUMA_E_HANDLE, reported. Thread and taking
       out as add. */
    int32_t (*remove)(const LumaMod* mod, LumaDrawLight light);
} LumaDrawLightService;

#ifdef __cplusplus
}
#endif

#endif
