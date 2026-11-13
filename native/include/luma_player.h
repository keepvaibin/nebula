/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's player service (contract services-player): extra players in the running scene, each a MarioActor of the
   game's own (the same brother as the game's player: the scene loaded only that one's files), driven by the second
   controller or by nothing. The game's own player stays the scene's: its camera, its HUD, its lives, the level's end.
   Import: LUMA_SERVICE(slot, "player", "1.0"). */
#ifndef LUMA_PLAYER_H
#define LUMA_PLAYER_H

#include <stdint.h>

#include "luma_content.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_PLAYER_NAME "player"
#define LUMA_PLAYER_MAJOR 1u
#define LUMA_PLAYER_MINOR 0u

/* Extra players standing at once, across mods (the game's own player not counted). */
#define LUMA_PLAYER_MOST_EXTRA 3u

/* What drives an extra player (LumaPlayerInit.input). */
typedef enum LumaPlayerInput {
    LUMA_PLAYER_INPUT_PAD = 0, /* the controller `pad`: its stick, buttons and shakes drive it as player 1's drive player 1 */
    LUMA_PLAYER_INPUT_NONE = 1 /* none: it stands, falls and is pushed as the game moves it; a mod moves it by the
                                  game's calls on it */
} LumaPlayerInput;

/* An extra player as make makes it. */
typedef struct LumaPlayerInit {
    uint32_t size;         /* sizeof(LumaPlayerInit) as the mod was built */
    LumaPlayerInput input; /* 4 bytes, a C enum */
    uint32_t pad;          /* LUMA_PLAYER_INPUT_PAD: 1, the game's second controller (input's LUMA_INPUT_PAD_CO_STAR); NONE: 0 */
    float pos[3];          /* where it stands, in the main stage's space; also where it comes back after a fall or a
                              death */
    float rot[3];          /* its turn in degrees, as a placement's */
} LumaPlayerInit; /* 1.0: 36 bytes */

typedef struct LumaPlayerService {
    LumaServiceTable base;
    /* 1.0. Game thread, while a scene with a player runs. An extra player standing at `init`'s place: LUMA_OK and *out,
       the MarioActor (LumaCore's class_of and is_a, the game's calls and hooks take it; a hook on MarioActor's code
       runs for it too: `this` tells them apart). Its own reads of the controller take init's (none: nothing pressed).
       It is hurt, falls and dies as the game's player does, but its death ends nothing: it comes back at init's place
       with its life full. It goes when the mod removes it, when the mod goes, or with its scene. A null init or out:
       LUMA_E_ARGUMENT, reported; an init smaller than 1.0's: LUMA_E_ARGUMENT, reported (Service.StructSize); an input
       outside the enum, a pad other than 1 for PAD or other than 0 for NONE, a place or turn that is not finite:
       LUMA_E_ARGUMENT, reported; LUMA_PLAYER_MOST_EXTRA standing: LUMA_E_LIMIT, reported; no scene with a player
       running: LUMA_E_STATE. Off the game thread: LUMA_E_THREAD, reported; while the game takes the mod out:
       LUMA_E_CANCELLED. */
    int32_t (*make)(const LumaMod* mod, const LumaPlayerInit* init, LumaActor* out);
    /* 1.0. Game thread. The mod's extra player goes: it is no longer moved, drawn or touched (MR::isDead on it is
       true). LUMA_OK; not one of the mod's standing extra players: LUMA_E_HANDLE, reported. Off the game thread:
       LUMA_E_THREAD, reported; while the game takes the mod out: LUMA_E_CANCELLED. */
    int32_t (*remove)(const LumaMod* mod, LumaActor player);
} LumaPlayerService;

#ifdef __cplusplus
}
#endif

#endif
