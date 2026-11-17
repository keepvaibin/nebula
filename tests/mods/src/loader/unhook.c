/* lt-unhook: hook and unhook while the game runs (loader contract 4). On MR::getRandom(0, 4247): a before (digit 1), a
   wrap (result = min * 10 + 3 after the function) and an after added by core->hook at start (result * 10 + 2). The
   after takes out the before and itself on its first call: a call already running keeps its hooks, so the first call
   gives 132; the second has the wrap alone: 3. unhook of a hook already gone: -1. A hook the mod never added: -1. */
#include "lt.h"

#define MARK 4247
static const LumaFunction* random2;
static int done;

static int32_t before(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == MARK) LUMA_ARG(call, 1, int32_t) = LUMA_ARG(call, 1, int32_t) * 10 + 1;
    return 0;
}

static int32_t wrap(LumaCall* call, void* user) {
    (void)user;
    lt_core->proceed(call);
    if (LUMA_ARG(call, 2, int32_t) == MARK) LUMA_RESULT(call, int32_t) = LUMA_ARG(call, 1, int32_t) * 10 + 3;
    return 0;
}

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != MARK) return 0;
    LUMA_RESULT(call, int32_t) = LUMA_RESULT(call, int32_t) * 10 + 2;
    if (!done) {
        done = 1;
        lt_fact("unhook_before", lt_core->unhook(lt_mod, random2, LUMA_BEFORE, before, 0));
        lt_fact("unhook_again", lt_core->unhook(lt_mod, random2, LUMA_BEFORE, before, 0));
        lt_fact("unhook_never", lt_core->unhook(lt_mod, random2, LUMA_AFTER, before, 0));
        lt_fact("unhook_self", lt_core->unhook(lt_mod, random2, LUMA_AFTER, after, 0));
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(random2, "MR::getRandom", "s32(s32, s32)"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, before),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_WRAP, wrap),
};

LT_START(lt_fact("hook_after", core->hook(mod, random2, LUMA_AFTER, after, 0)))
LUMA_MOD(imports, start, 0);
