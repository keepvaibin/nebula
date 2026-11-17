/* The guard follows the mod's call chain (native/host/luma_guard.cpp why_not_frames), in the game. Built with unwind
   data (-fasynchronous-unwind-tables) as guard-walk-<CASE>, an after hook on MarioActor::control that faults once:
   CASE 1: it logs through the kernel (a call of the game and the host that returns), then faults two of its own frames
           down, in a frame with room it never writes (the words the returned call left stay there);
   CASE 2: it faults two of its own frames down with no call out;
   CASE 3: the hook's own leaf faults.
   Each is resumed: the GUARD line, the mod off, the game goes on (f/tests/guard 1640-1642). FAULT=1 (guard_facts.h). */
#include "../guard/guard_facts.h"

#ifndef CASE
#error "CASE is 1 to 3"
#endif

#define NOINLINE __attribute__((noinline))

static int32_t done;

NOINLINE static int32_t leaf(void) {
    int32_t* volatile nowhere = 0;
    *nowhere = 1;
    return 0;
}

__attribute__((unused)) NOINLINE static int32_t inner(int32_t n) {
    return leaf() + n;
}

__attribute__((unused)) NOINLINE static int32_t outer(int32_t n) {
    volatile uint64_t room[96];
    room[0] = (uint64_t)n;
    return inner((int32_t)room[0]) + 1;
}

static int32_t hook(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (done) {
        fact("again", 1);
        return 0;
    }
    done = 1;
#if CASE == 1
    fact("entered", 1);
    for (int32_t i = 0; i < 8; i++) fact("logged", i);
    outer(3);
#elif CASE == 2
    outer(5);
#else
    leaf();
#endif
    fact("survived", 1);
    return 0;
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    g_core = core;
    g_mod = mod;
    fact("started", 1);
    return 0;
}

static void stop(void) {
    fact("stopped", 1);
}

static const LumaImport imports[] = {LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, hook)};
LUMA_MOD(imports, start, stop);
