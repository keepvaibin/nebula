/* Faults across mods (docs/modding/concepts/when-a-mod-fails.md, "A fault in another mod's code"): who the need walk
   turns off, the callback boundary (LumaCore.call_mod), the cut notice (LumaModInfo.cut). Every case is triggered by a
   step calling MR::getRandom(0, N) with the test's number N; the mods' after hooks act on their own N only.

   -DCASE=1, guard-cb-lib (a library, with a cut): exports cb.add(fn, who) (keeps a callback of `who`), cb.check() (fact
     "whole": its two-step state is whole) and cb.step(x, fn) (sets its state's first half, calls fn(x) plainly, sets
     the second half). Its after hook calls every kept callback with call_mod at 1644 (facts r<i>: call_mod's answer),
     plainly at 1645 and 1650 (facts d<i>: the result), and measures call_mod against a plain call at 1649 (facts
     cost_count, cost_plain, cost_guarded: ticks). Its cut puts the state right (second half = first) and says what it
     was told (cut_function, cut_caller, cut_where, cut_off, cut_faulted, cut_boundary, cut).
   -DCASE=2, guard-cb-bad: needs guard-cb-lib; keeps a callback that faults (access) on its first call (facts entered,
     survived), and at 1650 one that passes a null actor to MR::isDead (the game's code faults: not resumed).
   -DCASE=3, guard-cb-good: needs guard-cb-lib; keeps a callback that counts its calls (fact ran) and returns 7.
   -DCASE=4, guard-cb-stepper: needs guard-cb-lib; at 1647 calls cb.step(5, its own callback), which faults.
   -DCASE=5, guard-cb-checker: needs guard-cb-lib; at 1657 calls cb.check().
   -DCASE=6, guard-chain-a: needs guard-chain-l1; at 1646 calls l1.go (fact entered before, survived after).
   -DCASE=7, guard-chain-l1 (a library, with a cut): needs guard-chain-l2; l1.go calls l2.fault.
   -DCASE=8, guard-chain-l2 (a library, with a cut): l2.fault writes through a null pointer. */
#include "../loader/lt.h"

#define SIG "lumi1:0000000000001644"

/* fact <key> "<text>" (a mod's file, an id: no quote or backslash to escape). */
__attribute__((unused)) static void lt_text(const char* key, const char* text) {
    char line[200];
    char* at = lt_put(line, "fact ");
    at = lt_put(at, key);
    at = lt_put(at, " \"");
    for (const char* p = text != 0 ? text : ""; *p != 0 && at < line + sizeof(line) - 3; p++) {
        *at++ = (*p == '"' || *p == '\\') ? '_' : *p;
    }
    *at++ = '"';
    *at = 0;
    lt_core->log(lt_mod, line);
}

/* The test number an after hook on MR::getRandom(s32, s32) was called with (its second argument), or -1. */
#define CALLED_WITH(call) (LUMA_ARG(call, 1, int32_t) == 0 ? LUMA_ARG(call, 2, int32_t) : -1)

#if CASE == 1
typedef int32_t (*Callback)(int32_t);
static Callback fns[8];
static const LumaMod* whos[8];
static int32_t count;
static volatile int32_t first_half, second_half;

static int32_t add(Callback fn, const LumaMod* who) {
    if (count == 8) return -1;
    fns[count] = fn;
    whos[count] = who;
    return ++count;
}

static int32_t check(void) {
    lt_fact("whole", first_half == second_half ? 1 : 0);
    return first_half == second_half;
}

static int32_t step(int32_t x, Callback fn) {
    first_half = x;
    fn(x);
    second_half = x;
    return 0;
}

static uint64_t ticks(void) {
#if defined(__x86_64__)
    return __builtin_ia32_rdtsc();
#else
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#endif
}

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    const int32_t n = CALLED_WITH(call);
    char key[8] = {0};
    if (n == 1644) {
        for (int32_t i = 0; i < count; i++) {
            uint64_t result = 0;
            const int32_t answer = lt_core->call_mod(lt_mod, (const void*)fns[i], (uint64_t)i, 0, 0, 0, &result);
            key[0] = 'r';
            key[1] = (char)('0' + i);
            lt_fact(key, answer);
        }
        lt_fact("ran_all", 1);
    } else if (n == 1645 || n == 1650) {
        for (int32_t i = 0; i < count; i++) {
            const int32_t result = fns[i](n == 1650 ? 1650 : i);
            key[0] = 'd';
            key[1] = (char)('0' + i);
            lt_fact(key, result);
        }
        lt_fact("ran_all", 1);
    } else if (n == 1649 && count > 0) {
        enum { kCount = 20000 };
        volatile Callback plain = fns[0];
        int64_t sink = 0;
        const uint64_t t0 = ticks();
        for (int32_t i = 0; i < kCount; i++) sink += plain(1000000 + i);
        const uint64_t t1 = ticks();
        for (int32_t i = 0; i < kCount; i++) {
            uint64_t result = 0;
            lt_core->call_mod(lt_mod, (const void*)fns[0], (uint64_t)(1000000 + i), 0, 0, 0, &result);
            sink += (int64_t)result;
        }
        const uint64_t t2 = ticks();
        lt_fact("cost_count", kCount);
        lt_fact("cost_plain", (long long)(t1 - t0));
        lt_fact("cost_guarded", (long long)(t2 - t1));
        lt_fact("cost_sink", sink != 0);
    }
    return 0;
}

static void gone(const LumaMod* dependent) {
    int32_t kept = 0;
    for (int32_t i = 0; i < count; i++) {
        if (whos[i] != dependent) {
            fns[kept] = fns[i];
            whos[kept] = whos[i];
            kept++;
        }
    }
    count = kept;
    lt_fact("dependent_gone", 1);
}

static void cut(const LumaCut* c) {
    second_half = first_half;
    lt_text("cut_function", c->function);
    lt_text("cut_caller", c->caller_id);
    lt_text("cut_where", c->where);
    lt_text("cut_off", c->off);
    lt_text("cut_faulted", c->faulted);
    lt_fact("cut_boundary", c->boundary);
    lt_fact("cut", 1);
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("cb.add", SIG, add),
    LUMA_EXPORT_FUNCTION("cb.check", SIG, check),
    LUMA_EXPORT_FUNCTION("cb.step", SIG, step),
};

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START((void)0)
LUMA_LIBRARY_MOD_CUT(imports, start, 0, exports, gone, cut);

#elif CASE == 2 || CASE == 3 || CASE == 4 || CASE == 5
static const void* add_fn;
static const void* step_fn;
static const void* check_fn;
typedef int32_t (*AddFn)(int32_t (*)(int32_t), const LumaMod*);

#if CASE == 2
static const LumaFunction* is_dead;
static int32_t bad(int32_t i) {
    if (i == 1650) {
        /* The game's code faults (a null actor): outside the mods' code, so the game ends; the record names this mod. */
        void* actor = 0;
        uint8_t dead = 0;
        void* args[2] = {0, &actor};
        lt_fact("entered_game", 1);
        is_dead->invoke(args, &dead);
        lt_fact("survived_game", 1);
        return 0;
    }
    lt_fact("entered", 1);
    int32_t* volatile nowhere = 0;
    *nowhere = i;
    lt_fact("survived", 1);
    return 0;
}
#define KEEP bad
#elif CASE == 3
static int32_t calls;
static int32_t good(int32_t i) {
    /* 1649's timed calls (i from 1000000) count nothing: a fact per call would be what is timed. */
    if (i < 1000000) lt_fact("ran", ++calls);
    return 7;
}
#define KEEP good
#elif CASE == 4
static int32_t faulty(int32_t x) {
    lt_fact("entered", x);
    int32_t* volatile nowhere = 0;
    *nowhere = x;
    lt_fact("survived", 1);
    return 0;
}

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (CALLED_WITH(call) != 1647) return 0;
    ((int32_t (*)(int32_t, int32_t (*)(int32_t)))step_fn)(5, faulty);
    lt_fact("after_step", 1);
    return 0;
}
#else
static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (CALLED_WITH(call) != 1657) return 0;
    lt_fact("checked", ((int32_t (*)(void))check_fn)());
    return 0;
}
#endif

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(add_fn, "guard-cb-lib/cb.add", SIG),
    LUMA_MOD_FUNCTION(check_fn, "guard-cb-lib/cb.check", SIG),
    LUMA_MOD_FUNCTION(step_fn, "guard-cb-lib/cb.step", SIG),
#if CASE == 2
    LUMA_FUNCTION(is_dead, "MR::isDead", "bool(const LiveActor *)"),
#endif
#if CASE == 4 || CASE == 5
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
#endif
};

#ifdef KEEP
LT_START(((AddFn)add_fn)(KEEP, mod))
#else
LT_START((void)0)
#endif
LUMA_MOD(imports, start, 0);

#elif CASE == 6
static const void* go_fn;

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (CALLED_WITH(call) != 1646) return 0;
    lt_fact("entered", 1);
    lt_fact("result", ((int32_t (*)(int32_t))go_fn)(1));
    lt_fact("survived", 1);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(go_fn, "guard-chain-l1/l1.go", SIG),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);

#elif CASE == 7 || CASE == 8
static void cut(const LumaCut* c) {
    lt_text("cut_function", c->function);
    lt_text("cut_caller", c->caller_id);
    lt_text("cut_off", c->off);
    lt_text("cut_faulted", c->faulted);
    lt_fact("cut", 1);
}

#if CASE == 7
static const void* fault_fn;
static int32_t go(int32_t a) {
    lt_fact("going", 1);
    return ((int32_t (*)(int32_t))fault_fn)(a) + 1;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("l1.go", SIG, go),
};

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(fault_fn, "guard-chain-l2/l2.fault", SIG),
};
#else
static int32_t fault(int32_t a) {
    int32_t* volatile nowhere = 0;
    *nowhere = a;
    return a;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("l2.fault", SIG, fault),
};

static const LumaFunction* random_fn; /* bound, never called: a mod has at least one import */
static const LumaImport imports[] = {
    LUMA_FUNCTION(random_fn, "MR::getRandom", "s32(s32, s32)"),
};
#endif

LT_START((void)0)
LUMA_LIBRARY_MOD_CUT(imports, start, 0, exports, 0, cut);
#else
#error "CASE is 1 to 8"
#endif
