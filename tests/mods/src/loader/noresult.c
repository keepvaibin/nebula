/* A reference result left empty (loader contract 4, LR-1, D2-1): CameraLocalUtil::getVecReg gives const Vec3f &.
   A hook that neither writes it nor lets the function run leaves the slot null; the kernel then runs the function
   itself, the caller gets the function's own reference, and the kernel says so once per (function, mod)
   (Hook.NoResult), naming the hook's mod.
   lt-noresult      (-DCASE=1): a before that skips without writing.
   lt-noresult-wrap (-DCASE=2): a wrap that does not proceed and does not write (a wrap on a reference result binds).
   The harness cannot take a reference result, so the harness calls MR::getRandom(0, 4246) and this mod's after on
   it calls getVecReg three times itself (in a stage: the camera director must exist) and gives as the result how
   many calls returned a register (3: the function ran each time). CASE is a number. */
#include "lt.h"

#ifndef CASE
#error CASE
#endif

static const LumaFunction* vec_reg;
static const char kMine[] = "lt-noresult"; /* no such register: the game gives its dummy */

/* Only this mod's own calls (the name is its own string): the game's camera calls getVecReg every frame. */
static int32_t empty(LumaCall* call, void* user) {
    (void)user;
    const int mine = LUMA_ARG(call, 1, const char*) == kMine;
#if CASE == 1
    return mine; /* a before: skip, writing nothing */
#elif CASE == 2
    if (!mine) lt_core->proceed(call);
    return 0; /* a wrap: on this mod's calls, return without proceed, writing nothing */
#else
#error CASE is 1 or 2
#endif
}

static int32_t ask(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4246) return 0;
    int32_t ran = 0;
    for (int i = 0; i < 3; i++) {
        const char* name = kMine;
        void* args[2] = {0, (void*)&name};
        const void* got = 0;
        vec_reg->invoke(args, &got);
        if (got != 0) ran++;
    }
    LUMA_RESULT(call, int32_t) = ran;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("CameraLocalUtil::getVecReg", "const Vec3f &(const char *)", CASE == 1 ? LUMA_BEFORE : LUMA_WRAP, empty),
    LUMA_FUNCTION(vec_reg, "CameraLocalUtil::getVecReg", "const Vec3f &(const char *)"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, ask),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
