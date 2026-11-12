#pragma once

// The guard's kernel half (plan 1.6, contract guard, luma_guard.h): every place the kernel runs a mod's code calls it
// through run(), which pushes a frame the host's fault handler can resume at and calls the mod's function itself. No
// mod: nothing here runs.

#include <luma_guard.h>
#include <nebula_types.h>
#include <stdint.h>

struct LumaMod;

namespace luma {
    namespace guard {
        // Calls fn(a0, a1, a2, a3), a function of the mod's library with up to four integer or pointer arguments (more
        // are ignored by it), at a boundary of `kind` (LumaGuardBoundary) about `pWhere`. True: it returned, *pResult
        // its integer result (the low 32 bits for an int32_t; nothing for a void function). False: it did not run (the
        // mod is off after an earlier fault) or the guard caught a fault in it; *pResult is 0, the mod off for the
        // session, the player told (mod log GUARD) and its take-out queued; the caller goes on as if the code had done
        // nothing. Any thread; boundaries nest.
        bool run(LumaMod* pMod, u32 kind, const char* pWhere, const void* fn, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                 u64* pResult);

        // A fault was caught in the mod's code this session: none of its code runs again until the next start.
        bool faulted(const LumaMod* pMod);

        // A mod's library from the host's half: its image's base and its code (all 0 when there is none: its faults are
        // never resumed).
        void codeRange(void* pLibrary, uintptr_t* pBase, uintptr_t* pBegin, uintptr_t* pEnd);
        // In a harness run only (facts on), once, before the first mod starts: the fact guard.cost, the boundary's time
        // against a plain call and the cost of the clock's pair of reads in each boundary (contract guard 4). Nothing
        // otherwise.
        void measureForHarness();
        // Each mod's code time (luma_guard_cost): a slot from its load (game thread; a mod of the same id loaded again
        // starts a new ring), kept after it ends; frameEnd rolls every live slot once a frame (game thread, LumaLoader
        // frame). No mod: no slot, nothing rolled.
        void modBegin(LumaMod* pMod);
        void modEnd(LumaMod* pMod);
        void frameEnd();
        // luma_guard_install: the host's half, once, before any mod loads.
        bool install(const LumaGuardHost* pHost);
    }  // namespace guard
}  // namespace luma
