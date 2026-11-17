/* Mods for taking a mod out while the game runs (reload.md 2, 3; tests 1140-1142). Each has a stop, so `stopped` in
   takeout.<id> is true, and LUMA_MOD's begin and finish, so `finished` is true.
   lt-hold  (-DCASE=1): keeps three blocks, 100, 200 and 300 bytes (takeout.lt-hold: 3 blocks, 600 bytes); an after on
                        MR::getRandom(0, 4247) writes the fact `hooked` and gives 77, so a call after the take-out
                        shows the hook is gone (the count of `hooked` stays 1).
   lt-none  (-DCASE=2): keeps nothing (0 blocks, 0 bytes).
   lt-tlib  (-DCASE=3): a library: "lt.note" counts the calls; its stop writes `dependent_first`, 1 when lt-tuse's stop
                        called lt.note before it (dependents go first, reload.md 2).
   lt-tuse  (-DCASE=4): needs lt-tlib; its stop calls lt.note.
   lt-timer (-DCASE=5): a frames timer due in 100000 frames, still open at the take-out: the time service frees its
                        block at mod_end, so takeout.lt-timer has 0 blocks (H-RL1). */
#include "lt.h"

#ifndef CASE
#error CASE
#endif

#if CASE == 1
static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 4247) {
        lt_fact("hooked", 1);
        LUMA_RESULT(call, int32_t) = 77;
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START({
    void* const a = core->alloc(mod, 100, 8);
    void* const b = core->alloc(mod, 200, 8);
    void* const c = core->alloc(mod, 300, 8);
    lt_fact("held", a != 0 && b != 0 && c != 0);
})

static void stop(void) {
    lt_fact("stopped", 1);
}
LUMA_MOD(imports, start, stop);

#elif CASE == 2
LT_START({})

static void stop(void) {
    lt_fact("stopped", 1);
}
LUMA_MOD_NO_IMPORTS(start, stop);

#elif CASE == 3
static int32_t notes;

static void note(void) {
    notes++;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("lt.note", "lumi1:00000000000000b1", note),
};

LT_START({})

static void stop(void) {
    lt_fact("dependent_first", notes == 1);
}
/* A library with no imports: LUMA_LIBRARY_MOD takes the size of an imports array, so the form is written out. */
LUMA_RUNTIME
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, 0, 0, start, stop, luma_begin, luma_finish,
                                                 exports, sizeof(exports) / sizeof(exports[0]), 0, 0, 0};

#elif CASE == 4
typedef void (*Note)(void);
static const void* note_fn;

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(note_fn, "lt-tlib/lt.note", "lumi1:00000000000000b1"),
};

LT_START(lt_fact("bound", note_fn != 0))

static void stop(void) {
    if (note_fn != 0) ((Note)note_fn)();
    lt_fact("stopped", 1);
}
LUMA_MOD(imports, start, stop);

#else
#include <luma_time.h>

static const LumaTimeService* time_service;

static void due(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
    lt_fact("due", 1);
}

static const LumaImport imports[] = {
    LUMA_SERVICE(time_service, LUMA_TIME_NAME, "1.0"),
};

LT_START({
    LumaHandle timer = 0;
    const int32_t status = time_service->timer(mod, LUMA_TIME_FRAMES, 100000, 0, due, 0, &timer);
    lt_fact("timer_open", status == LUMA_PENDING && timer != 0);
})

static void stop(void) {
    lt_fact("stopped", 1);
}
LUMA_MOD(imports, start, stop);
#endif
