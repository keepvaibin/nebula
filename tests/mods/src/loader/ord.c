/* lt-ord1 / lt-ord2 (-DWHO=1 or 2): a before, a wrap and an after on MR::getRandom s32(s32, s32) (loader contract 1).
   Only a call whose max is 4242 is touched (the harness's; the game never asks for that range). Each hook appends one
   digit to the call's min, so the order the hooks ran is the number: before WHO, wrap entered WHO+2, wrap left WHO+4,
   after WHO+6; each after gives the min as the result, so the last after's is what the caller receives.
   With 1 above 2: 1 2 3 4 [function] 6 5 8 7 = 12346587. With 2 above 1: 21435678. */
#include "lt.h"

#ifndef WHO
#error WHO
#endif
#define MARK 4242

static int marked(LumaCall* call) {
    return LUMA_ARG(call, 2, int32_t) == MARK;
}

static void digit(LumaCall* call, int32_t d) {
    LUMA_ARG(call, 1, int32_t) = LUMA_ARG(call, 1, int32_t) * 10 + d;
}

static int32_t before(LumaCall* call, void* user) {
    (void)user;
    if (marked(call)) digit(call, WHO);
    return 0;
}

static int32_t wrap(LumaCall* call, void* user) {
    (void)user;
    const int m = marked(call);
    if (m) digit(call, WHO + 2);
    lt_core->proceed(call);
    if (m) digit(call, WHO + 4);
    return 0;
}

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (marked(call)) {
        digit(call, WHO + 6);
        LUMA_RESULT(call, int32_t) = LUMA_ARG(call, 1, int32_t);
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, before),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_WRAP, wrap),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
