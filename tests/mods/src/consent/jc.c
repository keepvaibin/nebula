/* J's consent mods (contract consent 2.6): code that does nothing but say it started (the fact <id>.started), so a
   test sees whether the question let it start. Its permissions are in its luma.json (build.sh); it imports no
   service, so each is optional until a mod imports one (consent 1.3 `required`). */
#include "../modlog/jl.h"

static int32_t pass(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, pass),
};

JL_START({})
LUMA_MOD(imports, start, 0);
