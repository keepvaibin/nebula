/* J's glTF material conformance mod (plan 3.3, contract materials; the lit template program_pbr.hlsli). Run steps call
   MR::getRandom(0, 3300 + k) and the hook gives 1. Each call's status is the fact mod.<id>.<name>.
   jt-spheres: k 1 attaches its two models at the player's base, a metre and a third to each side: jt-spheres:metal
   (G's metal-smooth.gltf: a 1 m sphere, base 0.95, metal 1, roughness 0.05) on the player's right (x -130: the actor faces +z, +x is its left) ("metal"), jt-spheres:rough
   (the same sphere, base red 0.8 0.15 0.1, metal 0, roughness 0.8; make_rough.py) on the left ("rough").
   jt-stripes (built with -DJT_STRIPES; plan 3.4, a mod's surface shader): k 1 attaches jt-stripes:stripes (the sphere
   white, through the mod's shaders/Stripes: blue bands by height) a metre and a third to the player's right
   ("stripes"). */
#include "../loader/lt.h"
#include <luma_content.h>
#include <luma_content_model.h>

static const LumaContentModelService* md;
static const LumaFunction* player_of; /* LiveActor* MR::getPlayerDemoActor() */
static LumaContentModel metal, rough, stripes;

static LumaActor player(void) {
    void* actor = 0;
    void* args[] = {0};
    player_of->invoke(args, &actor);
    return actor;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3300;
    if (k != 1) return 0;
    {
        const LumaPlace left = {sizeof(LumaPlace), {-130.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        const LumaPlace right = {sizeof(LumaPlace), {130.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
#ifdef JT_STRIPES
        (void)right;
        (void)metal;
        (void)rough;
        lt_fact("stripes", md->attach(lt_mod, player(), "jt-stripes:stripes", LUMA_CONTENT_MODEL_ACTOR, 0, &left, &stripes));
#else
        (void)stripes;
        lt_fact("metal", md->attach(lt_mod, player(), "jt-spheres:metal", LUMA_CONTENT_MODEL_ACTOR, 0, &left, &metal));
        lt_fact("rough", md->attach(lt_mod, player(), "jt-spheres:rough", LUMA_CONTENT_MODEL_ACTOR, 0, &right, &rough));
#endif
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(md, LUMA_CONTENT_MODEL_NAME, "1.0"),
    LUMA_FUNCTION(player_of, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
