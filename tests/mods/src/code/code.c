/* H's conformance mod for luma-format 6's code path: the entry library at code/<platform>/mod.<ending>, found by the
   host from luma.json `code.platforms` alone (no "plugins" list). It starts (fact started) and its hook on
   MR::getRandom runs: a run step calls MR::getRandom(0, 4280 + k) and the hook records k and gives 1. */
#include "../loader/lt.h"

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4280;
    if (k < 0 || k > 9) return 0;
    lt_fact("called", k);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
