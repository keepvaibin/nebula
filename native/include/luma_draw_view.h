/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's draw.view service (plan 2.4 layer 7; contract draw): the scene from another camera into a texture of the
   mod's: mirrors, picture in picture, replay cameras, minimaps. Import: LUMA_SERVICE(slot, "draw.view", "1.0").

   What a view shows: the scene's models (every list the game draws its world's models from: the map, objects, the
   player, enemies, the sky's models; opaque, then translucent) as this frame's poses have them, lit by the game's light
   sets, through the view's camera; cleared first to the view's colour. Not in it: particles, the game's shadow volumes,
   drawn lines and shapes, 2-D, the ray tracer's light (a view is drawn as with ray tracing off), and what the game's
   clipping left out of this frame for its own camera (a view shows what the game draws this frame). Drawn once a frame
   before the scene, so a view's texture drawn at a world point or on the HUD shows this frame. A view the mod has not
   given a camera draws nothing; its texture stays as last drawn (black at first). */
#ifndef LUMA_DRAW_VIEW_H
#define LUMA_DRAW_VIEW_H

#include <stdint.h>

#include "luma_content.h"
#include "luma_draw.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_DRAW_VIEW_NAME "draw.view"
#define LUMA_DRAW_VIEW_MAJOR 1u
#define LUMA_DRAW_VIEW_MINOR 0u

/* Views one mod has at once, and all mods together. */
#define LUMA_DRAW_VIEWS_MOST 2u
#define LUMA_DRAW_VIEWS_ALL_MOST 8u
/* A view's texture: at most this wide and high (pixels). */
#define LUMA_DRAW_VIEW_SIDE_MOST 1024u
/* A camera's vertical field of view: above 1 degree, under 179. */
#define LUMA_DRAW_VIEW_FOV_LEAST 1.0f
#define LUMA_DRAW_VIEW_FOV_MOST 179.0f

/* A view: owned by the mod until release or its end. */
typedef LumaHandle LumaDrawView;

typedef struct LumaDrawViewService {
    LumaServiceTable base;
    /* 1.0. Game thread. A view of w x h pixels (1 to LUMA_DRAW_VIEW_SIDE_MOST) with no camera yet, cleared to
       `clear` (0xRRGGBBAA). Its texture is one of the mod's draw textures (counted in LUMA_DRAW_TEXTURES_MOST and
       w x h x 4 in its bytes). LUMA_OK and *out. A size out of range, a null out: LUMA_E_ARGUMENT, reported;
       LUMA_DRAW_VIEWS_MOST of the mod's or ALL_MOST of all, or no texture room: LUMA_E_LIMIT, reported. Off the game
       thread: LUMA_E_THREAD; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*create)(const LumaMod* mod, uint32_t w, uint32_t h, LumaColor clear, LumaDrawView* out);
    /* 1.0. Game thread. The view's camera from now on: at `eye`, looking at `at`, `up` its up (world units and axes),
       `fov_y` degrees high, the picture's shape the view's w / h; it stops following. LUMA_OK. Not finite, `at` at the
       eye, `up` along the look or zero, fov out of range: LUMA_E_ARGUMENT, reported; an ended view or another mod's:
       LUMA_E_HANDLE, reported. */
    int32_t (*camera)(const LumaMod* mod, LumaDrawView view, const LumaDrawVec3* eye, const LumaDrawVec3* at,
                      const LumaDrawVec3* up, float fov_y);
    /* 1.0. Game thread. The view's camera follows `actor` each frame: the eye at `offset` from its place in the
       actor's own axes (its gravity's up as y), looking at the actor's place, the actor's up as up, `fov_y` degrees.
       Until camera, another follow, or the actor's death (the camera stays where it was last). A null, dead or
       non-actor `actor`, an offset not finite or of length 0, fov out of range: LUMA_E_ARGUMENT, reported. Handle as
       camera. */
    int32_t (*follow)(const LumaMod* mod, LumaDrawView view, LumaActor actor, const LumaDrawVec3* offset, float fov_y);
    /* 1.0. Game thread. The view's texture (a draw texture: draw.image, draw.billboard; not texture_update), the same
       handle each call; texture_free on it: LUMA_E_ARGUMENT. LUMA_OK and *out. Handle as camera; a null out:
       LUMA_E_ARGUMENT. */
    int32_t (*texture)(const LumaMod* mod, LumaDrawView view, LumaDrawTexture* out);
    /* 1.0. Game thread. The view drawn each frame (`on` 1, as made) or not (0: no cost; its texture keeps its last
       picture). Handle as camera. */
    int32_t (*set_on)(const LumaMod* mod, LumaDrawView view, int32_t on);
    /* 1.0. Game thread. The view ended and its texture freed (a draw of it after: the checker and LUMA_E_HANDLE).
       LUMA_OK; an ended view or another mod's: LUMA_E_HANDLE, reported. */
    int32_t (*release)(const LumaMod* mod, LumaDrawView view);
} LumaDrawViewService;

#ifdef __cplusplus
}
#endif

#endif
