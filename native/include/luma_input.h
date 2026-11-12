/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's input service (contract services-input): the mod's own actions, which the player binds in Controls.
   Import: LUMA_SERVICE(slot, "input", "1.0"). No permission: a mod reads only its own actions.

   An action is a `key` option of the mod's luma.json (luma-format revision 5): its `key` is the action's id, its `name`
   what Controls shows, its `default` the game control it follows until the player binds it (one of input_bindings.cpp's
   kTargetIds: "c" follows the game's C wherever the player has put it). In Controls the player gives it keys, mouse
   buttons or pad buttons of its own per scheme; from then on those, not the game control, press it. The bindings live
   only in the game's settings (`modbind.` lines), kept while the mod is off or removed. */
#ifndef LUMA_INPUT_H
#define LUMA_INPUT_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_INPUT_NAME "input"
#define LUMA_INPUT_MAJOR 1u
#define LUMA_INPUT_MINOR 0u

/* The most actions one mod has (its key options; luma-format allows 64 options of every kind). */
#define LUMA_INPUT_ACTIONS_MOST 64u

/* One of the mod's actions, as `action` gives it. Never 0; the same for the session. */
typedef LumaHandle LumaInputAction;

/* An action this game frame. `pressed` and `released` hold for the one frame the action went down or up; a press and
   a release inside one frame give both, with `down` as the frame ends. */
typedef struct LumaInputState {
    uint32_t size;     /* sizeof(LumaInputState) as the mod was built */
    uint8_t down;      /* 1 while held */
    uint8_t pressed;   /* 1 the frame it went down */
    uint8_t released;  /* 1 the frame it went up */
    uint8_t bound;     /* 1: the player bound it in Controls; 0: it follows its game control */
    float value;       /* 0 to 1: a trigger or stick bound to it pressed this far; 1 or 0 for a key or button */
    uint64_t frames;   /* game frames it has been down (0 while up) */
} LumaInputState;      /* 1.0: 24 bytes */

typedef struct LumaInputService {
    LumaServiceTable base;
    /* 1.0. Game thread. The mod's action `id` (its key option's key) into *out. LUMA_OK; LUMA_E_NOT_FOUND when the
       mod's luma.json has no key option of that key (an answer, not reported); a null id or out: LUMA_E_ARGUMENT,
       reported (Service.Argument). */
    int32_t (*action)(const LumaMod* mod, const char* id, LumaInputAction* out);
    /* 1.0. Game thread. The action's state this game frame into *out. LUMA_OK; an action that is not one of this mod's:
       LUMA_E_HANDLE, reported (Service.Handle); a null out: LUMA_E_ARGUMENT, a size below 1.0's: LUMA_E_ARGUMENT
       (Service.StructSize), both reported. While the player is in a menu of the host (Settings, the HOME menu) every
       action reads up. */
    int32_t (*read)(const LumaMod* mod, LumaInputAction action, LumaInputState* out);
} LumaInputService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_INPUT_H */
