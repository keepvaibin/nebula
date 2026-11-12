/* SPDX-License-Identifier: GPL-3.0-only */
/* References a mod makes that only code can satisfy (contract `references` section 3, "After start"): between the host
   and the game module; mods never call it. */
#ifndef LUMA_REFS_H
#define LUMA_REFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One placement of a class the validator could not check before the mods' code ran: the kept report's
   `touches.after_start` row, with the mod it is in. */
typedef struct LumaAfterStartRef {
    const char* mod;        /* the mod's id */
    const char* class_name; /* the placement row's class, as written */
    const char* file;       /* the placement's file inside the mod */
    uint32_t line;          /* its row, or 0 */
} LumaAfterStartRef;

/* The game module's export, found by name ("luma_refs_check_after_start"). The host calls it once, after the last
   mod's start and before the title, with every mod's after_start rows; with no mod it is not called. It looks each
   class up in the factory (the game's classes and those mods made), keeps a CONTENT entry Content.MissingClass (620) in
   the mod log for each one missing, and from then on a placement of that class makes the placeholder object
   (references section 4). Copies everything it keeps; the host's strings may go when it returns. Game thread or the
   host's start thread, before the first scene. */
typedef void (*LumaRefsCheckAfterStart)(const LumaAfterStartRef* refs, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
