/* tt-global: a global by its address (types 1.2, kind 7; abi addition 5). CubeCameraArea::sCubeCategory is an s32
   the game writes in CubeCameraArea::setCurrentCategory(s32) (1 << category).
   - On MR::getRandom(0, 4311): the mod keeps the value, calls setCurrentCategory(3) itself and reads 8 at its address
     (mod_call3); writes 0x7777 there and reads it back (own_write).
   - The harness then calls setCurrentCategory(5); an after hook on it reads 32 at the address (game_call5).
   - On MR::getRandom(0, 4312): the kept value goes back (restored).
   The same address holds what the game writes, called by the mod or by the game's own caller. */
#include "../loader/lt.h"

static const LumaFunction* set_category;
static int32_t* category;
static int32_t kept;

static int32_t after_set(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 1, int32_t);
    if (k == 5) lt_fact("game_call5", *category);
    return 0;
}

static int32_t marks(LumaCall* call, void* user) {
    (void)user;
    const int32_t m = LUMA_ARG(call, 2, int32_t);
    if (m == 4311) {
        kept = *category;
        int32_t three = 3;
        void* args[2] = {0, &three};
        set_category->invoke(args, 0);
        lt_fact("mod_call3", *category);
        *(volatile int32_t*)category = 0x7777;
        lt_fact("own_write", *(volatile int32_t*)category);
        LUMA_RESULT(call, int32_t) = 1;
    } else if (m == 4312) {
        *category = kept;
        lt_fact("restored", *category == kept);
        LUMA_RESULT(call, int32_t) = 1;
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_GLOBAL(category, "CubeCameraArea::sCubeCategory", "s32"),
    LUMA_FUNCTION(set_category, "CubeCameraArea::setCurrentCategory", "void(s32)"),
    LUMA_HOOK_ON("CubeCameraArea::setCurrentCategory", "void(s32)", LUMA_AFTER, after_set),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, marks),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
