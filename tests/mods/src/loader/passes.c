/* lt-passes: the game step's passes that may run on worker threads in the plain module (P-040: the frustum judge, the
   shadows' first part, actor animation) run on the game thread when a mod hooks a function inside them (loader
   contract 4: a hook runs on its caller's thread; the plain module, with no hook points, is loaded only when no mod
   has code). A before hook on one function of each pass counts its calls on and off the game thread;
   MR::getRandom(0, 4249) gives the facts: <name>_on (at least 1) and <name>_off (0). */
#include "lt.h"

enum { kJudge, kJudgeIn, kClipped, kShadow, kGravity, kCalcAnim, kModelAnim, kXanime, kCount };

static const char* const on_keys[kCount] = {"judge_on", "judge_in_on", "clipped_on", "shadow_on", "gravity_on",
                                            "calc_anim_on", "model_anim_on", "xanime_on"};
static const char* const off_keys[kCount] = {"judge_off", "judge_in_off", "clipped_off", "shadow_off", "gravity_off",
                                             "calc_anim_off", "model_anim_off", "xanime_off"};
static volatile int32_t on_count[kCount], off_count[kCount];

static int32_t counted(LumaCall* call, void* user) {
    (void)call;
    const int32_t k = (int32_t)(intptr_t)user;
    __atomic_add_fetch(lt_core->game_thread() ? &on_count[k] : &off_count[k], 1, __ATOMIC_RELAXED);
    return 0;
}

static int32_t report(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4249) return 0;
    for (int32_t k = 0; k < kCount; k++) {
        lt_fact(on_keys[k], __atomic_load_n(&on_count[k], __ATOMIC_RELAXED));
        lt_fact(off_keys[k], __atomic_load_n(&off_count[k], __ATOMIC_RELAXED));
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

/* A before hook on `name` counting into slot k (the import's user value). */
#define COUNTED(name, signature, k) {LUMA_IMPORT_HOOK, 0, name, signature, 0, counted, LUMA_BEFORE, (void*)(intptr_t)(k)}

static const LumaImport imports[] = {
    COUNTED("ClippingJudge::isJudgedToClipFrustum", "bool(const Vec3f &, f32) const", kJudge),
    COUNTED("ClippingJudge::isJudgedToClipFrustum", "bool(const Vec3f &, f32, s32) const", kJudgeIn),
    COUNTED("MR::isClipped", "bool(const LiveActor *)", kClipped),
    COUNTED("ShadowController::updateDirection", "void()", kShadow),
    COUNTED("PlanetGravity::calcGravity", "bool(Vec3f *, const Vec3f &) const", kGravity),
    COUNTED("LiveActor::calcAnim", "void()", kCalcAnim),
    COUNTED("ModelManager::calcAnim", "void()", kModelAnim),
    COUNTED("XanimePlayer::calcAnm", "void(u32)", kXanime),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, report),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
