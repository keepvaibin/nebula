/* SPDX-License-Identifier: GPL-3.0-only */
/* Between the host and the game's LumaCore loader (docs/modding/lumalink/loader.md); not for mods. */
#ifndef LUMA_HOST_H
#define LUMA_HOST_H

#include <stdint.h>

#include "luma.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_REFUSAL_MOST 8

/* Why a mod was refused, as the validator says its findings: rule `code` (505 is L505) with its parameters,
   names[i] = values[i]. A value "T:<text>" is a literal, "P:<phrase id>" a phrase (luma_validate_message renders
   both). The place is given as "mod" and "file". The values are whole (never cut) and kept by the game for the rest
   of the session, so the Mods page may show them at any time. */
typedef struct LumaRefusal {
    uint16_t code; /* 0: none */
    uint32_t count;
    const char* names[LUMA_REFUSAL_MOST];
    const char* values[LUMA_REFUSAL_MOST];
} LumaRefusal;

typedef void (*LumaCloseLibrary)(void* library);

/* The game module's exports, found by name ("luma_load_mod", "luma_unload_mod").

   luma_load_mod binds a mod's library whole or not at all, at `order` (its place in the Mods list, top 0), and runs
   its start. 1: on. 0: refused, `refusal` says why. On the game thread. From this call on the game closes `library`
   with `close`: at once when none of the mod's code can still run, else after the grace (another thread may be
   inside one of its hooks). The host never closes it. `granted` names the permissions the player granted this mod
   (`granted_count` of them, copied): a service that needs another is refused (docs/modding/lumalink/loader.md, Services).

   luma_unload_mod takes a mod out: its hooks, its stop, then after the grace its destructors, its memory and its
   library. */
typedef int32_t (*LumaLoadMod)(const LumaModInfo* info, const char* mod_id, const char* file, int32_t order, void* library,
                               LumaCloseLibrary close, const char* const* granted, uint32_t granted_count, LumaRefusal* refusal);
typedef void (*LumaUnloadMod)(const LumaModInfo* info);

/* The start in two steps, so every mod's layers call is answered before any mod starts ("luma_begin_mod",
   "luma_mod_layers", "luma_start_begun", "luma_drop_begun"; on the game thread).

   luma_begin_mod takes the arguments of luma_load_mod and binds the library as it does, runs its begin, then asks its
   layers call (LumaModInfo.layers), and leaves the mod waiting: later mods bind to its exports. 1: waiting. 0: refused,
   as luma_load_mod refuses; a fault in its layers call refuses it as a fault in its start does.

   luma_mod_layers gives what a waiting or started mod's layers call answered: the names, each with its NUL, and their
   bytes in *bytes. Null (and 0): no layers call, no name, or no such mod. Valid until the mod ends.

   luma_start_begun runs a waiting mod's start and publishes its hooks, as the second half of luma_load_mod. 1: on.
   0: refused, `refusal` says why.

   luma_drop_begun ends a waiting mod that will not start (a mod it needs did not start): the mods it imported from are
   told, and its library closes, as after a refused start.

   luma_load_mod stays one call (a reload): it does not ask the layers call. */
typedef int32_t (*LumaBeginMod)(const LumaModInfo* info, const char* mod_id, const char* file, int32_t order, void* library,
                                LumaCloseLibrary close, const char* const* granted, uint32_t granted_count, LumaRefusal* refusal);
typedef const char* (*LumaModLayers)(const LumaModInfo* info, uint64_t* bytes);
typedef int32_t (*LumaStartBegun)(const LumaModInfo* info, LumaRefusal* refusal);
typedef void (*LumaDropBegun)(const LumaModInfo* info);

/* luma_take_out_request asks for a mod to be taken out while the game runs (reload.md 2), from any thread. At the
   start of the next frame the game thread takes out the mods that use it (through their mod imports, transitively),
   the last started first, then the mod, each as luma_unload_mod does. A second request for a queued id is one
   request; a mod that is not running is said in the log and left. 1: queued; 0: not (null, empty or over 63 bytes,
   or 16 requests waiting; said in the log).

   luma_set_taken_out gives the game a function it tells each mod's id once that mod is taken out (on the game
   thread, after its stop; its library closes after the grace). Null: nobody is told. */
typedef int32_t (*LumaTakeOutRequest)(const char* mod_id);
typedef void (*LumaTakenOut)(const char* mod_id);
typedef void (*LumaSetTakenOut)(LumaTakenOut taken_out);

#ifdef __cplusplus
}
#endif

#endif
