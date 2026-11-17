/* A fixed result (loader contract 4, LR-1): GameDataFunction::makeGalaxyScenarioAccessor gives a
   GameDataSomeScenarioAccessor by value, a class with no default constructor, so a hook cannot leave it unbuilt.
   lt-noskip  (-DCASE=1): a wrap on it: refused at bind, L512 Hook.NoDefaultResult, the mod off.
   lt-skipref (-DCASE=2): a before on it that asks to skip: not taken, the function runs, and the kernel says so once
   (Hook.SkipRefused) however often it asks. The harness cannot take a class result, so the harness calls
   MR::getRandom(0, 4245) and this mod's after on it calls the function three times itself and gives as the result
   how many calls built the accessor (3 when the function ran).
   A test of the kernel: the accessor's scenario number is read by its layout (after a pointer), not by a field
   import. CASE is a number: the preprocessor does not know luma.h's enums. */
#include "lt.h"

#ifndef CASE
#error CASE
#endif

#define NAME "GameDataFunction::makeGalaxyScenarioAccessor"
#define SIG "GameDataSomeScenarioAccessor(const char *, s32)"

#if CASE == 1
static int32_t wrap(LumaCall* call, void* user) {
    (void)user;
    lt_core->proceed(call);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON(NAME, SIG, LUMA_WRAP, wrap),
};
#elif CASE == 2
typedef struct {
    void* storage;
    uint32_t scenario;
    uint32_t rest;
} Accessor;

static const LumaFunction* make;
static const char kGalaxy[] = "EggStarGalaxy";

/* Only this mod's own calls (the name is its own string), not the game's. */
static int32_t skip(LumaCall* call, void* user) {
    (void)user;
    return LUMA_ARG(call, 1, const char*) == kGalaxy; /* asks to skip, without writing: the result has no empty value */
}

static int32_t ask(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4245) return 0;
    int32_t ran = 0;
    for (int i = 0; i < 3; i++) {
        const char* galaxy = kGalaxy;
        int32_t scenario = 1;
        void* args[3] = {0, (void*)&galaxy, (void*)&scenario};
        Accessor got;
        unsigned char* bytes = (unsigned char*)&got;
        for (unsigned b = 0; b < sizeof(got); b++) bytes[b] = 0xA5;
        make->invoke(args, &got);
        if (got.scenario == 1u) ran++; /* 0xA5A5A5A5 unless the accessor was built */
    }
    LUMA_RESULT(call, int32_t) = ran;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON(NAME, SIG, LUMA_BEFORE, skip),
    LUMA_FUNCTION(make, NAME, SIG),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, ask),
};
#else
#error CASE is 1 or 2
#endif

LT_START((void)0)
LUMA_MOD(imports, start, 0);
