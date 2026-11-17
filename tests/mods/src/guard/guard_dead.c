/* The guard's cases that are not resumed (contract guard.md 2): each must end the game with a message naming the mod
   (Windows: the crash reporter's report carries the guard's sentence; POSIX: one line on stderr), never silently and
   never by resuming.
   -DCASE=1, mod guard-deep: unbounded recursion in an after hook on MarioActor::control (a stack overflow).
   -DCASE=2, mod guard-game: an after hook on MarioActor::control passes a null actor to MR::isDead, so the fault is in
   the game's code, not the mod's (rule 3).
   -DCASE=3, mod crash-start: unbounded recursion in its start (a crash while the game starts: the next start leaves
   the mod off, services/mods.md "After a crash").
   -DCASE=4, mod crash-stop: unbounded recursion in its stop (a crash while the mod is taken out in play: the next
   start leaves the mod off).
   Facts: "entered 1" before; "survived 1" must never appear. */
#define FAULT 1 /* guard_facts.h's fault_here is not used here */
#include "guard_facts.h"

static int32_t done;

#if CASE == 1 || CASE == 3 || CASE == 4
/* Each level keeps a page of its own on the stack and uses it after the call, so no level is a tail call and none is
   folded away. */
static volatile int32_t bottom = -1; /* never reached: the compiler cannot see that the recursion has no end */

static int32_t deeper(int32_t level) {
    if (level == bottom) {
        return 0;
    }
    volatile uint8_t page[4096];
    page[0] = (uint8_t)level;
    page[4095] = (uint8_t)(level >> 8);
    int32_t below = deeper(level + 1);
    return below + page[0] + page[4095];
}
#endif

#if CASE == 1
static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (done) {
        return 0;
    }
    done = 1;
    fact("entered", 1);
    int32_t got = deeper(1);
    fact("survived", got);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
};

#elif CASE == 2
static const LumaFunction* is_dead;

static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (done) {
        return 0;
    }
    done = 1;
    fact("entered", 1);
    const void* nobody = 0;
    /* args[0] is `this` (none for a free function); the parameter is args[1] (luma.h). */
    void* args[2] = {0, &nobody};
    _Bool dead = 0;
    is_dead->invoke(args, &dead);
    fact("survived", dead);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
    LUMA_FUNCTION(is_dead, "MR::isDead", "bool(const LiveActor *)"),
};
#elif CASE == 3 || CASE == 4
static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
};
#else
#error "CASE is 1 (stack overflow), 2 (a fault in the game's code), 3 (a stack overflow in the start) or 4 (one in the stop)"
#endif

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    g_core = core;
    g_mod = mod;
    fact("started", 1);
#if CASE == 3
    if (!done) {
        done = 1;
        fact("entered", 1);
        fact("survived", deeper(1));
    }
#endif
    return 0;
}

#if CASE == 4
static void stop(void) {
    fact("entered", 1);
    fact("survived", deeper(1));
}
#else
#define stop 0
#endif

LUMA_MOD(imports, start, stop);
