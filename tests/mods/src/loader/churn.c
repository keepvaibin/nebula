/* lt-churn: hook and unhook every frame for 600 frames (loader contract 4: a chain is never changed in place; the old
   ones wait on the retire list). An after on GameSystem::update adds a before on MR::getRandom on even frames and takes
   it out on odd ones; each change must answer 0. The harness asks MR::getRandom(0, 4250) at the end for the count. */
#include "lt.h"

static const LumaFunction* random2;
static int32_t frames, changes, failures;

static int32_t extra(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    return 0;
}

static int32_t every_frame(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (frames >= 600) return 0;
    const int32_t rc = (frames & 1) == 0 ? lt_core->hook(lt_mod, random2, LUMA_BEFORE, extra, 0)
                                        : lt_core->unhook(lt_mod, random2, LUMA_BEFORE, extra, 0);
    if (rc == 0) changes++;
    else failures++;
    frames++;
    return 0;
}

static int32_t report(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4250) return 0;
    lt_fact("frames", frames);
    lt_fact("changes", changes);
    lt_fact("failures", failures);
    LUMA_RESULT(call, int32_t) = changes;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(random2, "MR::getRandom", "s32(s32, s32)"),
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_AFTER, every_frame),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, report),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
