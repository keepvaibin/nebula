/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's draw.view service (docs/modding/services/draw.md): the scene from another camera into a texture of the
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

/* A view (the script's view.Region): owned by the mod until release or its end. */
typedef LumaHandle LumaDrawViewRegion;

/* Where a camera is and how it looks (a script's view.Eye): world units and axes. */
typedef struct LumaDrawViewEye {
    float eye[3];
    float at[3];  /* the point looked at; not at the eye */
    float up[3];  /* not along the look, not zero */
    float fov_y;  /* degrees high, above LUMA_DRAW_VIEW_FOV_LEAST, under LUMA_DRAW_VIEW_FOV_MOST */
} LumaDrawViewEye;

/* What a rig's eye follows. */
typedef enum LumaDrawViewRig {
    LUMA_DRAW_VIEW_RIG_FIXED = 0,  /* eye, at, up as given */
    LUMA_DRAW_VIEW_RIG_FOLLOW = 1, /* the eye at `offset` from `actor` in its own axes (x side, y its gravity's up, z front),
                                      looking at its place, its up as up */
    LUMA_DRAW_VIEW_RIG_JOINT = 2,  /* the eye at `actor`'s joint `joint`, looking along the actor's front, its up as up
                                      (first person); `near` the near plane */
    LUMA_DRAW_VIEW_RIG_WORLD = 3   /* the eye at `offset` from `actor` in world axes, looking at its place, its up as up
                                      (side cameras) */
} LumaDrawViewRig;

/* A camera rig (a script's view.Camera): the same rig drives a view (rig) or, through eye_of, the main camera
   (LumaKit camera). */
typedef struct LumaDrawViewCamera {
    LumaDrawViewRig rig;
    float eye[3];       /* FIXED */
    float at[3];        /* FIXED */
    float up[3];        /* FIXED */
    float fov_y;        /* every rig, as LumaDrawViewEye's */
    LumaActor actor;    /* FOLLOW, JOINT, WORLD: a live actor */
    float offset[3];    /* FOLLOW, WORLD: not of length 0 */
    char joint[32];     /* JOINT: a joint of the actor's model, NUL-terminated */
    float near_z;       /* JOINT: the near plane in game units, above 0; 0 elsewhere: the game's */
} LumaDrawViewCamera;

typedef struct LumaDrawViewService {
    LumaServiceTable base;
    /* 1.0. Game thread. A view of w x h pixels (1 to LUMA_DRAW_VIEW_SIDE_MOST) with no camera yet, cleared to
       `clear` (0xRRGGBBAA). Its texture is one of the mod's draw textures (counted in LUMA_DRAW_TEXTURES_MOST and
       w x h x 4 in its bytes). LUMA_OK and *out. A size out of range, a null out: LUMA_E_ARGUMENT, reported;
       LUMA_DRAW_VIEWS_MOST of the mod's or ALL_MOST of all, or no texture room: LUMA_E_LIMIT, reported. Off the game
       thread: LUMA_E_THREAD; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*create)(const LumaMod* mod, uint32_t w, uint32_t h, LumaColor clear, LumaDrawViewRegion* out);
    /* 1.0. Game thread. The view's camera from now on: at `eye`, looking at `at`, `up` its up (world units and axes),
       `fov_y` degrees high, the picture's shape the view's w / h; it stops following. LUMA_OK. Not finite, `at` at the
       eye, `up` along the look or zero, fov out of range: LUMA_E_ARGUMENT, reported; an ended view or another mod's:
       LUMA_E_HANDLE, reported. */
    int32_t (*camera)(const LumaMod* mod, LumaDrawViewRegion view, const LumaDrawVec3* eye, const LumaDrawVec3* at,
                      const LumaDrawVec3* up, float fov_y);
    /* 1.0. Game thread. The view's camera follows `actor` each frame: the eye at `offset` from its place in the
       actor's own axes (its gravity's up as y), looking at the actor's place, the actor's up as up, `fov_y` degrees.
       Until camera, another follow, or the actor's death (the camera stays where it was last). A null, dead or
       non-actor `actor`, an offset not finite or of length 0, fov out of range: LUMA_E_ARGUMENT, reported. Handle as
       camera. */
    int32_t (*follow)(const LumaMod* mod, LumaDrawViewRegion view, LumaActor actor, const LumaDrawVec3* offset,
                      float fov_y);
    /* 1.0. Game thread. The view's texture (a draw texture: draw.image, draw.billboard; not texture_update), the same
       handle each call; texture_free on it: LUMA_E_ARGUMENT. LUMA_OK and *out. Handle as camera; a null out:
       LUMA_E_ARGUMENT. */
    int32_t (*texture)(const LumaMod* mod, LumaDrawViewRegion view, LumaDrawTexture* out);
    /* 1.0. Game thread. The view drawn each frame (`on` 1, as made) or not (0: no cost; its texture keeps its last
       picture). LUMA_OK; handle as camera. */
    int32_t (*set_on)(const LumaMod* mod, LumaDrawViewRegion view, int32_t on);
    /* 1.0. Game thread. The view ended and its texture freed (a draw of it after: the checker and LUMA_E_HANDLE).
       LUMA_OK; an ended view or another mod's: LUMA_E_HANDLE, reported. */
    int32_t (*release)(const LumaMod* mod, LumaDrawViewRegion view);
    /* 1.0. Game thread. The view's camera follows `camera` each frame from now on (a FIXED rig is `camera` once), until
       camera, follow, another rig, or the rig's actor's death (the camera stays where it was last). LUMA_OK. A null
       camera, an unknown rig, a rig's field out of its range (named): LUMA_E_ARGUMENT, reported; a joint the actor's
       model lacks: LUMA_E_ARGUMENT naming "joint". Handle as camera. */
    int32_t (*rig)(const LumaMod* mod, LumaDrawViewRegion view, const LumaDrawViewCamera* camera);
    /* 1.0. Game thread. Where `camera`'s eye is this frame (the main camera's for LumaKit camera): LUMA_OK and *out.
       Arguments as rig, a null out: LUMA_E_ARGUMENT, reported. The rig's actor dead: LUMA_E_STATE, not reported (the
       caller keeps its last eye). Never needs a view. */
    int32_t (*eye_of)(const LumaMod* mod, const LumaDrawViewCamera* camera, LumaDrawViewEye* out);
    /* 1.0. Game thread. The main camera at `eye` (null: the game's own again) from now on: each frame, before the
       scene's models take their poses, the scene's view and field of view are set from it, so it holds while the
       scene's update is held (a pause, a cutscene's hold) and what reads the camera until the game's camera moves again
       (clipping, the stick's directions, sound) reads it; the game's camera keeps working out its own pose underneath
       and has it back at once on null. The newest mod to call holds it; one mod's null gives back only its own. LUMA_OK.
       An eye not finite, `at` at the eye, `up` along the look or zero, fov out of range: LUMA_E_ARGUMENT, reported.
       Ends with the mod. */
    int32_t (*main_eye)(const LumaMod* mod, const LumaDrawViewEye* eye);
} LumaDrawViewService;

#ifdef __cplusplus
}
#endif

#endif
