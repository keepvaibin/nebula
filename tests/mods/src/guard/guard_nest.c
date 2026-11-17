/* The guard's nested case (contract guard.md 1: boundaries nest per thread; the innermost names the mod).
   -DCASE=1, mod guard-nest-a: an after hook on MarioActor::control (X) calls MR::getRandom(777, 778) (Y) each frame and
   logs "a_calls <n>" once the call returned: A goes on, its count keeps rising after B's fault.
   -DCASE=2 -DFAULT=<1..3>, mod guard-nest-b-<fault>: a before hook on Y faults when its range is A's (777, 778), so only
   inside A's call; the game's own calls of Y pass. B is named and taken out; "survived" and "again" never appear.
   Load A and B together; A must not be taken out (its "stopped" fact comes only at the session's end). */
#ifndef FAULT
#define FAULT 1
#endif
#include "guard_facts.h"

#if CASE == 1
static const LumaFunction* random_range;
static int32_t calls;

static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    int32_t low = 777, high = 778, got = 0;
    /* args[0] is `this` (none for a free function); the parameters start at 1 (luma.h). */
    void* args[3] = {0, &low, &high};
    random_range->invoke(args, &got);
    calls++;
    if (calls <= 8 || calls % 60 == 0) {
        fact("a_calls", calls);
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
    LUMA_FUNCTION(random_range, "MR::getRandom", "s32(s32, s32)"),
};

#elif CASE == 2
static int32_t done;

static int32_t before_random(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, int32_t) != 777 || LUMA_ARG(call, 2, int32_t) != 778) {
        return 0;
    }
    if (done) {
        fact("again", 1);
        return 0;
    }
    done = 1;
    fault_here();
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, before_random),
};
#else
#error "CASE is 1 (mod A) or 2 (mod B)"
#endif

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    g_core = core;
    g_mod = mod;
    fact("started", 1);
    return 0;
}

static void stop(void) {
    fact("stopped", 1);
}

LUMA_MOD(imports, start, stop);
