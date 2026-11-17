/* Kernel freeze review conformance mods (tests 1160-1169). Facts are log lines "fact <key> <n>".
   k1-body (-DCASE=1, test 1160): the last hooks of a function taken out from its own before, so the function's own
       code runs with its flag clear. On MR::getRandom(0, 4247) (s32) the before takes out itself and the mod's after
       (unhook_self 0, unhook_after 0); the call keeps the chain it started with, so that after still runs once the
       body returned. It hooks a wrap that gives -5 without proceeding (hook_again 0) and calls the function at once:
       rehooked must be -5. A stale tBody left by the body makes that call skip its hooks and give a random number in
       [0, 4247). The check runs before any other hooked call can overwrite tBody: the s32 body calls
       MR::getRandom(f32, f32), which has no hook here.
   k1-stop (-DCASE=2, test 1161): its stop calls core->hook (an after on MR::getRandom(s32, s32)). A mod being taken
       out adds no hook: stop_hook must be -1, and a call of the function after its library closed must not reach
       the mod (hooked_after_stop never written; the game does not crash). */
#include <luma.h>

#ifndef CASE
#error CASE
#endif

static const LumaCore* k_core;
static const LumaMod* k_mod;

static char* k_put(char* at, const char* s) {
    while (*s != 0) *at++ = *s++;
    return at;
}

static void k_fact(const char* key, long long v) {
    char line[160];
    char digits[24];
    int n = 0;
    unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
    char* at = k_put(line, "fact ");
    at = k_put(at, key);
    *at++ = ' ';
    do {
        digits[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u != 0);
    if (v < 0) *at++ = '-';
    while (n > 0) *at++ = digits[--n];
    *at = 0;
    k_core->log(k_mod, line);
}

#define MARK 4247
__attribute__((unused)) static const LumaFunction* random2;

#if CASE == 1
static int32_t gives_minus5(LumaCall* call, void* user) {
    (void)user;
    LUMA_RESULT(call, int32_t) = -5;
    return 0;
}

static int32_t taken_out;
static int32_t after_s32(LumaCall* call, void* user);

static int32_t before_s32(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == MARK && taken_out == 0) {
        k_fact("unhook_self", k_core->unhook(k_mod, random2, LUMA_BEFORE, before_s32, 0));
        k_fact("unhook_after", k_core->unhook(k_mod, random2, LUMA_AFTER, after_s32, 0));
        taken_out = 1;
    }
    return 0;
}

static int32_t after_s32(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (taken_out != 1) return 0;
    taken_out = 2;
    k_fact("hook_again", k_core->hook(k_mod, random2, LUMA_WRAP, gives_minus5, 0));
    int32_t lo = 0;
    int32_t hi = MARK;
    void* args[3] = {0, &lo, &hi};
    int32_t r = 0;
    random2->invoke(args, &r);
    k_fact("rehooked", r);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(random2, "MR::getRandom", "s32(s32, s32)"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, before_s32),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after_s32),
};

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    k_core = core;
    k_mod = mod;
    k_fact("started", 1);
    return 0;
}
LUMA_MOD(imports, start, 0);

#elif CASE == 3
/* k1-count (test 1167): counts the calls of MR::getRandom(1.5, 4267.5) (f32) its before sees. */
static int32_t count_f32(LumaCall* call, void* user) {
    (void)user;
    static long long calls;
    if (LUMA_ARG(call, 2, float) == 4267.5f) k_fact("count", ++calls);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "f32(f32, f32)", LUMA_BEFORE, count_f32),
};

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    k_core = core;
    k_mod = mod;
    k_fact("started", 1);
    return 0;
}
LUMA_MOD(imports, start, 0);

#elif CASE == 4
/* k1-fault (test 1167): MR::getRandom(1.5, 4267.5)'s own code calls MR::getRandom() (f32). Armed by a before on the
   first, its before on the second faults (a write to address 0) while the first's body runs: the guard resumes at the
   second's boundary, inside that body, and the mod is off. */
static int32_t armed;
static int* volatile k_nowhere;

static int32_t arm(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, float) == 4267.5f && armed == 0) {
        armed = 1;
        k_fact("armed", 1);
    }
    return 0;
}

static int32_t fault(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (armed == 1) {
        armed = 2;
        *k_nowhere = 1;
        k_fact("survived", 1);
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "f32(f32, f32)", LUMA_BEFORE, arm),
    LUMA_HOOK_ON("MR::getRandom", "f32()", LUMA_BEFORE, fault),
};

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    k_core = core;
    k_mod = mod;
    k_fact("started", 1);
    return 0;
}
LUMA_MOD(imports, start, 0);

#else
static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == MARK) k_fact("hooked_after_stop", 1);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(random2, "MR::getRandom", "s32(s32, s32)"),
};

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    k_core = core;
    k_mod = mod;
    k_fact("started", 1);
    return 0;
}

static void stop(void) {
    k_fact("stop_hook", k_core->hook(k_mod, random2, LUMA_AFTER, after, 0));
}
LUMA_MOD(imports, start, stop);
#endif
