/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's anim service (docs/modding/content/animation.md): a clip of an actor's model played as the game
   plays its own (its joint, texture, colour, texture-swap and visibility clips of one name started together), from a
   frame, at a rate, once or looping, blended from the pose it had; its frame read; frames of it marked, each mark an
   event as the clip passes it; its end an event. The clip is the model's own (a mod model's glTF animations, a game
   model's clips, or the mod's replacement of one: contract animation 1 and 2). Import: LUMA_SERVICE(slot, "anim",
   "1.0"). */
#ifndef LUMA_ANIM_H
#define LUMA_ANIM_H

#include <stdint.h>

#include "luma_content.h"
#include "luma_content_model.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_ANIM_NAME "anim"
#define LUMA_ANIM_MAJOR 1u
#define LUMA_ANIM_MINOR 1u

/* Plays one mod has going at once, marks one play carries, and event rows one clip's events give. */
#define LUMA_ANIM_MOST_PLAYS 64u
#define LUMA_ANIM_MOST_MARKS 16u
#define LUMA_ANIM_MOST_EVENT_ROWS 64u
/* An event row's name: 1 to 63 bytes of A-Z a-z 0-9 _ . (the cook's rule). */
#define LUMA_ANIM_EVENT_NAME_MOST 63u
/* The fastest rate: frames of the clip per frame of the game (60 a second). */
#define LUMA_ANIM_MOST_RATE 16.0f

/* A play: owned by the mod, ended by its end, stop, another play of a kind it plays on its actor, the actor's own
   code starting another clip, the actor's death, its scene's end, or the mod's. */
typedef LumaHandle LumaAnimPlay;

/* The events a play gives (LumaEvent.kind; handle: the play). */
enum {
    LUMA_ANIM_EVENT_END = 1, /* status LUMA_OK: a once clip reached its last frame; LUMA_E_CANCELLED: it ended first.
                                value: a LumaAnimWhy. Last of its play's events. */
    LUMA_ANIM_EVENT_MARK = 2 /* status LUMA_OK; value: below LumaAnimHow.mark_count the mark's index in its marks;
                                1.1: from mark_count up, mark_count + the row of the clip's own events (the cooked
                                <clip>.events beside it, animation 7.3), data the row's name (UTF-8 text;
                                data_size bytes, no NUL, valid during the call). A clip without events gives only the
                                mod's marks. */
};

/* Why a play ended (LUMA_ANIM_EVENT_END value). */
typedef enum LumaAnimWhy {
    LUMA_ANIM_ENDED = 0,    /* a once clip at its last frame */
    LUMA_ANIM_STOPPED = 1,  /* the mod's stop */
    LUMA_ANIM_REPLACED = 2, /* another play of a kind it plays on its actor, or the actor's own code playing another
                               clip of its first kind */
    LUMA_ANIM_ACTOR = 3,    /* the actor died (a model's: it was detached) */
    LUMA_ANIM_SCENE = 4,    /* the actor's scene ended */
    LUMA_ANIM_MOD = 5       /* the mod stopped or was taken out (no event comes then) */
} LumaAnimWhy;

/* How a clip plays (LumaAnimHow.loop). */
typedef enum LumaAnimLoop {
    LUMA_ANIM_AS_CLIP = 0, /* as the clip says (its glTF extras.nebula.play, or the game's clip) */
    LUMA_ANIM_ONCE = 1,    /* once, then it stays on its last frame */
    LUMA_ANIM_LOOP = 2     /* from its first frame again after its last, never ending */
} LumaAnimLoop;

/* Blend frames (LumaAnimHow.blend). */
#define LUMA_ANIM_BLEND_CLIP 0    /* as the game starts the clip: its model's clip table (.banmt), else none */
#define LUMA_ANIM_BLEND_NONE (-1) /* the clip's pose at once */

/* How a play goes: zeroed (or a null pointer) is the clip as the game plays it, from its first frame. */
typedef struct LumaAnimHow {
    uint32_t size;        /* sizeof(LumaAnimHow) as the mod was built */
    float rate;           /* frames of the clip per frame of the game; 0: 1. Above 0, at most LUMA_ANIM_MOST_RATE */
    int32_t blend;        /* joint clips: frames from the pose it had to the clip's; LUMA_ANIM_BLEND_CLIP or _NONE */
    uint32_t loop;        /* a LumaAnimLoop: 0 to 2 */
    float start;          /* the frame it starts from: 0 to the clip's last */
    uint32_t mark_count;  /* at most LUMA_ANIM_MOST_MARKS */
    const float* marks;   /* frames (copied) that each give LUMA_ANIM_EVENT_MARK as the clip passes them, every time it
                             does (each loop; either way on a clip that goes there and back) */
} LumaAnimHow; /* 1.0: 32 bytes with 8-byte pointers, 28 with 4-byte */

typedef struct LumaAnimService {
    LumaServiceTable base;
    /* 1.0. Game thread. The clip `clip` of `actor`'s model played: every kind of clip of that name its model has (joints
       .bck, texture moves .btk, colours .brk and .bpk, texture swaps .btp, visibility .bva), the first of them in that
       order leading (its frame is the play's; its end, its marks; 1.1: its events' rows, as marks after the mod's own,
       the first LUMA_ANIM_MOST_EVENT_ROWS of them). An earlier play on the actor of a kind this one
       plays ends (LUMA_ANIM_REPLACED). Played as `how` says (null: the clip as the game plays it, from its first
       frame). The play's events come to fn(user, event) at a frame's start (`fn` null: none; `user` null: given back
       null). LUMA_OK and *out. No clip of that name: LUMA_E_NOT_FOUND, Content.MissingAnimation in the mod log. A null
       or dead actor, one that is not one or has no model, an empty name, a rate below 0, not a number or above
       LUMA_ANIM_MOST_RATE, a blend below -1, a loop outside the enum, a start outside the clip, more than
       LUMA_ANIM_MOST_MARKS marks, null marks with a count, a null out: LUMA_E_ARGUMENT, reported; a how below 1.0's
       size: LUMA_E_ARGUMENT, reported (Service.StructSize); LUMA_ANIM_MOST_PLAYS going: LUMA_E_LIMIT, reported. Off
       the game thread: LUMA_E_THREAD, reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    /* events: END(socket, status, value), MARK(socket, value, data) */
    int32_t (*play)(const LumaMod* mod, LumaActor actor, const char* clip, const LumaAnimHow* how, LumaEventFn fn,
                    void* user, LumaAnimPlay* out);
    /* 1.0. Game thread. As play, on the model `model` the mod attached (content.model, models 7): its own clips. A
       skeleton-follow model's joints stay its actor's; its other kinds play. `how`, `fn` and `user` as play. LUMA_OK and
       *out. No clip of that name, or a model that shows nothing (neither it nor the placeholder in a store):
       LUMA_E_NOT_FOUND, Content.MissingAnimation in the mod log. An ended model: LUMA_E_HANDLE, reported. A clip, how
       or out play refuses: LUMA_E_ARGUMENT, reported; LUMA_ANIM_MOST_PLAYS going: LUMA_E_LIMIT, reported. Off the game
       thread: LUMA_E_THREAD, reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    /* events: END(socket, status, value), MARK(socket, value, data) */
    int32_t (*play_on_model)(const LumaMod* mod, LumaContentModel model, const char* clip, const LumaAnimHow* how,
                             LumaEventFn fn, void* user, LumaAnimPlay* out);
    /* 1.0. Game thread. The play stops where it is: every kind of it stays on its frame until its actor's code or
       another play starts a clip. Its end: LUMA_E_CANCELLED, LUMA_ANIM_STOPPED. LUMA_OK; an ended play:
       LUMA_E_HANDLE, reported; off the game thread: LUMA_E_THREAD, reported; while the game takes the mod out:
       LUMA_E_CANCELLED. */
    int32_t (*stop)(const LumaMod* mod, LumaAnimPlay play);
    /* 1.0. Game thread. The play's frame now into *frame (null: not wanted) and its clip's last frame into *last
       (null: not wanted). LUMA_OK; an ended play: LUMA_E_HANDLE, reported; off the game thread: LUMA_E_THREAD,
       reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*frame)(const LumaMod* mod, LumaAnimPlay play, float* frame, float* last);
} LumaAnimService;

#ifdef __cplusplus
}
#endif

#endif
