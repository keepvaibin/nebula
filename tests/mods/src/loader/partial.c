/* lt-partial: a mod binds whole or not at all (loader contract 2). Its first row is a hook that would answer
   MR::getRandom(4249, 4249) with 999; its second names a function the game does not have. Refused, L505 naming it; the
   hook from the first row must not be left behind (the call gives the game's 4249). */
#include "lt.h"

static const LumaFunction* nothing;

static int32_t hook(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 4249) {
        LUMA_RESULT(call, int32_t) = 999;
        return 1;
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, hook),
    LUMA_FUNCTION(nothing, "MR::ltNoSuchFunction", "void()"),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
