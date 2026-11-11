/* SPDX-License-Identifier: GPL-3.0-only */
/* Between the host and the game's LumaCore loader (loader contract 3 and LR-5); not for mods. */
#ifndef LUMA_HOST_H
#define LUMA_HOST_H

#include <stdint.h>

#include "luma.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_REFUSAL_MOST 8
#define LUMA_REFUSAL_VALUE 256

/* Why a mod was refused, as the validator says its findings: rule `code` (505 is L505) with its parameters,
   names[i] = values[i]. A value "T:<text>" is a literal, "P:<phrase id>" a phrase (luma_validate_message renders
   both). The place is given as "mod" and "file". */
typedef struct LumaRefusal {
    uint16_t code; /* 0: none */
    uint32_t count;
    const char* names[LUMA_REFUSAL_MOST];
    char values[LUMA_REFUSAL_MOST][LUMA_REFUSAL_VALUE];
} LumaRefusal;

typedef void (*LumaCloseLibrary)(void* library);

/* The game module's exports, found by name ("luma_load_mod", "luma_unload_mod").

   luma_load_mod binds a mod's library whole or not at all, at `order` (its place in the Mods list, top 0), and runs
   its start. 1: on. 0: refused, `refusal` says why. On the game thread. From this call on the game closes `library`
   with `close`: at once when none of the mod's code can still run, else after the grace (another thread may be
   inside one of its hooks). The host never closes it.

   luma_unload_mod takes a mod out: its hooks, its stop, then after the grace its destructors, its memory and its
   library. */
typedef int32_t (*LumaLoadMod)(const LumaModInfo* info, const char* mod_id, const char* file, int32_t order, void* library,
                               LumaCloseLibrary close, LumaRefusal* refusal);
typedef void (*LumaUnloadMod)(const LumaModInfo* info);

#ifdef __cplusplus
}
#endif

#endif
