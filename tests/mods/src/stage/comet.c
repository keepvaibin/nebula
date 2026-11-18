/* 3904-comet-star-achievement (P-164). cm-probe at run step MR::getRandom(0, 3904): "comet_no", EggStarGalaxy's
   comet scenario (GalaxyStatusAccessor::getNormalCometScenarioNo), the star id cm-star's PowerStar has (Obj_arg0);
   "comet_red", its comet is the speedy (red) one. */
#include "../loader/lt.h"

static const LumaFunction* f_accessor;
static const LumaFunction* f_comet_no;
static const LumaFunction* f_comet_name;

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, int32_t) != 0 || LUMA_ARG(call, 2, int32_t) != 3904) return 0;
    const char* galaxy = "EggStarGalaxy";
    void* accessor = 0; /* GalaxyStatusAccessor: one pointer, its ScenarioData */
    void* make_args[2] = {0, (void*)&galaxy};
    f_accessor->invoke(make_args, &accessor);
    int32_t comet = 0;
    void* no_args[1] = {(void*)&accessor};
    f_comet_no->invoke(no_args, &comet);
    lt_fact("comet_no", comet);
    const char* name = 0;
    void* name_args[2] = {(void*)&accessor, (void*)&comet};
    f_comet_name->invoke(name_args, &name);
    lt_fact("comet_red", name != 0 && name[0] == 'R' && name[1] == 'e' && name[2] == 'd' && name[3] == 0);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(f_accessor, "MR::makeGalaxyStatusAccessor", "GalaxyStatusAccessor(const char *)"),
    LUMA_FUNCTION(f_comet_no, "GalaxyStatusAccessor::getNormalCometScenarioNo", "s32() const"),
    LUMA_FUNCTION(f_comet_name, "GalaxyStatusAccessor::getCometName", "const char *(s32) const"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
