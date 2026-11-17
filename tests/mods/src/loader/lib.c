/* lt-lib: a library mod (loader D3-3). Exports, sorted by name: "lt.add", "lt.ready" (1 once its start has run: a
   mod that needs it reads 1 at its own start only if lt-lib started first, P5-1), "lt.remember" (a mod gives its own
   LumaMod, so dependent_gone can be checked to name it, P5-2) and "lt.twice". They are plain C functions, not R-2's
   form (caller, file, line, out; 0/1/2): this tests the kernel, which binds only the address; R-2's form is abi item 8
   in phase 4 (P5-3). A before on MR::getRandom(0, 4253) appends digit 2, so with lt-uses (digit 1) the call's number
   gives the hooks' order: the list's, whatever order the needs made them start in. Told dependent_gone: facts of how
   often and whether it named the remembered mod, and one line. */
#include "lt.h"

#define SIG "lumi1:00000000000000a1"

static int32_t gone_count;
static int32_t ready;
static const LumaMod* remembered;

static int32_t add(int32_t a, int32_t b) {
    return a + b;
}

static int32_t is_ready(void) {
    return ready;
}

static void remember(const LumaMod* mod) {
    remembered = mod;
}

static int32_t twice(int32_t a) {
    return a * 2;
}

static void dependent_gone(const LumaMod* dependent) {
    gone_count++;
    lt_fact("gone", gone_count);
    lt_fact("gone_remembered", dependent != 0 && dependent == remembered);
    lt_say("told: a mod that used it is gone");
}

static int32_t before(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 4253) LUMA_ARG(call, 1, int32_t) = LUMA_ARG(call, 1, int32_t) * 10 + 2;
    return 0;
}

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 4253) LUMA_RESULT(call, int32_t) = LUMA_ARG(call, 1, int32_t);
    return 0;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("lt.add", SIG, add),
    LUMA_EXPORT_FUNCTION("lt.ready", "lumi1:00000000000000a3", is_ready),
    LUMA_EXPORT_FUNCTION("lt.remember", "lumi1:00000000000000a4", remember),
    LUMA_EXPORT_FUNCTION("lt.twice", "lumi1:00000000000000a2", twice),
};

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, before),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START(ready = 1)
LUMA_LIBRARY_MOD(imports, start, 0, exports, dependent_gone);
