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
                        block at mod_end, so takeout.lt-timer has 0 blocks (H-RL1).
   lt-name  (-DCASE=6): MR::getRandom(0, 4410) names the player with a literal in the mod's image (N-4094): the game
                        keeps the pointer, so a find by that name after the take-out reads it.
   lt-name-made (-DCASE=7): MR::getRandom(0, 4411) names the player with a text made while running, kept with
                        keep_text (the game's memory, not the mod's: takeout.lt-name-made has 0 blocks). `kept`: one
                        was given; `same`: equal bytes give the same text; `copied`: not the mod's own buffer.
                        Then 1000 short texts and 20 of 4000 bytes, each kept twice: `many_same` (every second keep
                        gives the first pointer) and `many_right` (every copy holds its bytes and a NUL).
   lt-name-pair (-DCASE=8): MR::getRandom(0, 4412), after lt-name-made is taken out, keeps the same text: `shared` is 1
                        when it is the very pointer the player's name holds (one copy for every mod, still there). */
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

#elif CASE == 6 || CASE == 7 || CASE == 8
static const LumaFunction* player;
static const LumaFunction* set_name;
static int32_t name_at; /* NameObj.mName */

__attribute__((unused)) static void name_player(const char* name) {
    void* args[1] = {0};
    void* got = 0;
    player->invoke(args, &got);
    if (got == 0) {
        lt_fact("named", 0);
        return;
    }
    void* set[2] = {got, (void*)&name}; /* a pointer parameter: args[1] points at it */
    set_name->invoke(set, 0);
    lt_fact("named", 1);
}

#if CASE == 7 || CASE == 8
/* keep_text when this game's LumaCore has it; else a block of the mod's own, as a mod built for an older game would. */
static const char* keep(const char* text, uint64_t length) {
    if (lt_core->size >= __builtin_offsetof(LumaCore, keep_text) + sizeof(lt_core->keep_text) && lt_core->keep_text != 0) {
        return lt_core->keep_text(lt_mod, text, length);
    }
    char* const copy = (char*)lt_core->alloc(lt_mod, length + 1, 1);
    if (copy != 0) {
        for (uint64_t i = 0; i < length; i++) copy[i] = text[i];
        copy[length] = 0;
    }
    return copy;
}
#endif

static int32_t after(LumaCall* call, void* user) {
    (void)user;
#if CASE == 7
    if (LUMA_ARG(call, 2, int32_t) == 4411) {
        static const char* firsts[1020];
        static char big[4001];
        int same = 1;
        int right = 1;
        for (int round = 0; round < 2; round++) {
            for (int i = 0; i < 1020; i++) {
                char small[24];
                const char* text = small;
                uint64_t length;
                if (i < 1000) {
                    length = (uint64_t)(lt_num(lt_put(small, "t"), i) - small);
                } else {
                    for (int k = 0; k < 4000; k++) big[k] = (char)('a' + (k + i) % 26);
                    big[4000] = 0;
                    text = big;
                    length = 4000;
                }
                const char* const kept = keep(text, length);
                if (round == 0) {
                    firsts[i] = kept;
                } else if (kept != firsts[i]) {
                    same = 0;
                }
                if (kept == 0 || kept[length] != 0) {
                    right = 0;
                } else {
                    for (uint64_t k = 0; k < length; k++) {
                        if (kept[k] != text[k]) right = 0;
                    }
                }
            }
        }
        lt_fact("many_same", same);
        lt_fact("many_right", right);
    }
#endif
#if CASE == 6
    if (LUMA_ARG(call, 2, int32_t) == 4410) {
        name_player("lt-name literal");
    }
#elif CASE == 8
    if (LUMA_ARG(call, 2, int32_t) == 4412) {
        char made[32];
        char* const end = lt_num(lt_put(made, "lt-name made "), 4411);
        const char* const mine = keep(made, (uint64_t)(end - made));
        void* args[1] = {0};
        void* got = 0;
        player->invoke(args, &got);
        const char* name = 0;
        if (got != 0 && name_at >= 0) {
            name = *(const char* const*)((const unsigned char*)got + name_at);
        }
        lt_fact("shared", mine != 0 && mine == name);
    }
#else
    if (LUMA_ARG(call, 2, int32_t) == 4411) {
        char made[32];
        char* const end = lt_num(lt_put(made, "lt-name made "), 4411);
        const char* const first = keep(made, (uint64_t)(end - made));
        const char* const second = keep(made, (uint64_t)(end - made));
        lt_fact("kept", first != 0);
        lt_fact("same", first != 0 && first == second);
        lt_fact("copied", first != 0 && first != made);
        if (first != 0) name_player(first);
    }
#endif
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(player, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_FUNCTION(set_name, "NameObj::setName", "void(const char *)"),
    LUMA_FIELD(name_at, "NameObj.mName", "const char *"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START({})

static void stop(void) {
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
