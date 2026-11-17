/* lt-sighas: L505 lists the game's signatures for a name it has (validator revision 5; the loader says the same).
   CASE 1: a hook on MR::getPlayerLeft asked as "int()" (the game has "s32()"): Name.FunctionSigHas.
   CASE 2: a virtual call of LiveActor::calcAndSetBaseMtx asked as "int()" (the game has "void()"): Name.MethodHas.
   CASE 3: MR::getRandom with no signature (the game has three): Name.FunctionMany.
   Each is refused before any code runs, so `started` is never said. */
#include "lt.h"

#if CASE == 1
static int32_t hook(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getPlayerLeft", "int()", LUMA_BEFORE, hook),
};
#elif CASE == 2
static const LumaFunction* calc;

static const LumaImport imports[] = {
    LUMA_METHOD(calc, "LiveActor::calcAndSetBaseMtx", "int()"),
};
#else
static const LumaFunction* any_random;

static const LumaImport imports[] = {
    LUMA_FUNCTION(any_random, "MR::getRandom", 0),
};
#endif

LT_START((void)0)
LUMA_MOD(imports, start, 0);
