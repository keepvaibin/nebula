/* jl-say<WHO> (test 3124): one Error while playing. The test calls MR::getRandom(0, 4300 + WHO) on the game thread;
   this mod's after-hook reports once (jl_say.lumi, line WHO) when the call is its own, and leaves every other call
   alone. Seven such mods in one frame: three notices, the rest wait; more than three waiting fold. */
#include "jl.h"

#ifndef WHO
#error WHO
#endif

static int32_t said(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4300 + WHO) return 0;
    jl_report("jl_say.lumi", WHO);
    LUMA_RESULT(call, int32_t) = WHO;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, said),
};

JL_START({})
LUMA_MOD(imports, start, 0);
