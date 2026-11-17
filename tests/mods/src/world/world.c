/* FB's world-draw and draw.inst conformance mods (plan 2.4 layers 3 and 5; native/include/luma_draw.h 3-D calls,
   luma_draw_inst.h), one case each (-DCASE=n). Run steps call MR::getRandom(0, 2449 + k) and the hook gives 1; k 1 to
   9 are the case's steps. Each call's status is the fact mod.<id>.<name> (the first frame's, for calls made every
   frame).
   fw-world (1) 2450: k 1 draw at world.after every frame: a red line3 from the player's feet 300 up, 8 wide; a tri3
                 red/green/blue 100 over the player (corners 100 apart); a 4x4 checker from pixels on an 80x80
                 billboard 250 over the player ("at", "line3", "tri3", "billboard", "pixels"); k 2 at(world.after,
                 null): nothing from the next frame ("off").
   fw-args  (2) 2451: k 1 line3 outside a point ("outside": E_STATE); then at world.after once: line3 with a NaN end
                 ("nan": E_ARGUMENT), thickness 0 ("thin": E_ARGUMENT), tri3 null ("tri_null": E_ARGUMENT), a freed
                 texture's billboard ("freed": E_HANDLE, the checker), billboard of size -1 ("size": E_ARGUMENT), mesh
                 with an empty name ("mesh_name": E_ARGUMENT), mesh "Coin" at the player ("mesh": LUMA_OK), mesh
                 "FwNoModel" ("no_model": E_NOT_FOUND).
   fi-gold  (3) 2452: k 1 draw.inst.of the player ("of"), its colour gold 0xFFC83CFF ("colour"); k 2 the joint HandR
                 hidden ("hide"); k 3 drawn 1.5 times ("scale"); k 4 released ("release").
   fi-args  (4) 2453: of a null actor ("null_actor": E_ARGUMENT), a null out ("null_out": E_ARGUMENT); of the player
                 ("of"), again ("again": 1 when the same handle); set_material_param "FiNoName" ("bad_name":
                 E_ARGUMENT), material0 at 2 ("bad_value": E_ARGUMENT), on material "FiNoMaterial" ("no_material":
                 E_NOT_FOUND), material0 red on every material ("param"); set_visible "FiNoJoint" ("no_part":
                 E_NOT_FOUND); set_scale 0 ("zero_scale": E_ARGUMENT); set_texture ("texture": E_ABSENT); release
                 ("release"), again ("release2": E_HANDLE), set_colour on it ("colour_ended": E_HANDLE).
   fw-mesh  (5) 2454: its own model (layers/base/assets/metre.gltf, the 1 m cube on a joint 1 m up, cooked to
                 Luma/fw-mesh/metre): k 1 at world.after every frame, draw.mesh "Luma/fw-mesh/metre" 150 to the
                 player's +x at his feet ("mesh"; first frame), "Luma/fw-mesh/FwNone" ("missing": E_NOT_FOUND);
                 k 2 off ("off"). */
#include "../loader/lt.h"
#include <luma_draw.h>
#include <luma_draw_inst.h>

#ifndef CASE
#error CASE
#endif

#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

typedef struct Vec3 {
    float x, y, z;
} Vec3;

static const LumaDrawService* draw;
__attribute__((unused)) static const LumaDrawInstService* inst;
static const LumaFunction* get_player_pos; /* Vec3f* MR::getPlayerPos() */
__attribute__((unused)) static const LumaFunction* player_of; /* LiveActor* MR::getPlayerDemoActor() */
__attribute__((unused)) static int32_t frames;

__attribute__((unused)) static Vec3 player_pos(void) {
    const Vec3* at = 0;
    void* none[] = {0};
    get_player_pos->invoke(none, &at);
    Vec3 out = {0, 0, 0};
    if (at != 0) out = *at;
    return out;
}

__attribute__((unused)) static LumaActor player(void) {
    void* actor = 0;
    void* args[] = {0};
    player_of->invoke(args, &actor);
    return actor;
}

__attribute__((unused)) static LumaDrawTexture checker;

__attribute__((unused)) static int32_t make_checker(void) {
    uint8_t pixels[4 * 4 * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint8_t* p = pixels + (y * 4 + x) * 4;
            const int lit = ((x ^ y) & 1) == 0;
            p[0] = lit ? 255 : 0;
            p[1] = lit ? 255 : 0;
            p[2] = lit ? 0 : 255;
            p[3] = 255;
        }
    }
    return draw->texture_from_pixels(lt_mod, 4, 4, pixels, 0, &checker);
}

#if CASE == 1
static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
    const int first = frames++ == 0;
    const Vec3 p = player_pos();
    const LumaDrawVec3 a = {p.x, p.y, p.z};
    const LumaDrawVec3 b = {p.x, p.y + 300.0f, p.z};
    int32_t s = draw->line3(lt_mod, &a, &b, 8.0f, RGBA(255, 0, 0, 255));
    if (first) lt_fact("line3", s);
    const LumaDrawVec3 tri[3] = {{p.x - 50.0f, p.y + 100.0f, p.z}, {p.x + 50.0f, p.y + 100.0f, p.z}, {p.x, p.y + 186.0f, p.z}};
    const LumaColor colours[3] = {RGBA(255, 0, 0, 255), RGBA(0, 255, 0, 255), RGBA(0, 0, 255, 255)};
    s = draw->tri3(lt_mod, tri, colours);
    if (first) lt_fact("tri3", s);
    const LumaDrawVec3 at = {p.x, p.y + 250.0f, p.z};
    s = draw->billboard(lt_mod, checker, &at, 80.0f, 80.0f, RGBA(255, 255, 255, 255));
    if (first) lt_fact("billboard", s);
}
#elif CASE == 2
static int painted;
static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
    if (painted++ != 0) return;
    const Vec3 p = player_pos();
    const float zero = 0.0f;
    const LumaDrawVec3 a = {p.x, p.y, p.z};
    const LumaDrawVec3 nan_ = {p.x, zero / zero, p.z};
    lt_fact("nan", draw->line3(lt_mod, &a, &nan_, 8.0f, RGBA(255, 0, 0, 255)));
    lt_fact("thin", draw->line3(lt_mod, &a, &a, 0.0f, RGBA(255, 0, 0, 255)));
    lt_fact("tri_null", draw->tri3(lt_mod, 0, 0));
    const LumaDrawVec3 at = {p.x, p.y + 200.0f, p.z};
    lt_fact("freed", draw->billboard(lt_mod, checker, &at, 80.0f, 80.0f, RGBA(255, 255, 255, 255)));
    lt_fact("size", draw->billboard(lt_mod, checker, &at, -1.0f, 80.0f, RGBA(255, 255, 255, 255)));
    const float m[12] = {1, 0, 0, p.x, 0, 1, 0, p.y + 100.0f, 0, 0, 1, p.z};
    lt_fact("mesh_name", draw->mesh(lt_mod, "", m, RGBA(255, 255, 255, 255)));
    lt_fact("mesh", draw->mesh(lt_mod, "Coin", m, RGBA(255, 255, 255, 255)));
    lt_fact("no_model", draw->mesh(lt_mod, "FwNoModel", m, RGBA(255, 255, 255, 255)));
}
#elif CASE == 5
static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
    const int first = frames++ == 0;
    const Vec3 p = player_pos();
    const float m[12] = {1, 0, 0, p.x + 150.0f, 0, 1, 0, p.y, 0, 0, 1, p.z};
    const int32_t s = draw->mesh(lt_mod, "Luma/fw-mesh/metre", m, RGBA(255, 255, 255, 255));
    if (first) {
        lt_fact("mesh", s);
        lt_fact("missing", draw->mesh(lt_mod, "Luma/fw-mesh/FwNone", m, RGBA(255, 255, 255, 255)));
    }
}
#endif

#if CASE == 3 || CASE == 4
static LumaDrawInst gold;
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2449;
    if (k < 1 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        lt_fact("pixels", make_checker());
        lt_fact("at", draw->at(lt_mod, LUMA_DRAW_WORLD_AFTER, paint, 0));
    }
    if (k == 2) lt_fact("off", draw->at(lt_mod, LUMA_DRAW_WORLD_AFTER, 0, 0));
#elif CASE == 2
    if (k == 1) {
        const LumaDrawVec3 a = {0, 0, 0};
        lt_fact("outside", draw->line3(lt_mod, &a, &a, 8.0f, RGBA(255, 0, 0, 255)));
        lt_fact("pixels", make_checker());
        lt_fact("free", draw->texture_free(lt_mod, checker));
        lt_fact("at", draw->at(lt_mod, LUMA_DRAW_WORLD_AFTER, paint, 0));
    }
#elif CASE == 3
    if (k == 1) {
        lt_fact("of", inst->of(lt_mod, player(), &gold));
        lt_fact("colour", inst->set_colour(lt_mod, gold, RGBA(255, 200, 60, 255)));
    }
    if (k == 2) lt_fact("hide", inst->set_visible(lt_mod, gold, "HandR", 0));
    if (k == 3) {
        const float s[3] = {1.5f, 1.5f, 1.5f};
        lt_fact("scale", inst->set_scale(lt_mod, gold, s));
    }
    if (k == 4) lt_fact("release", inst->release(lt_mod, gold));
#elif CASE == 4
    if (k == 1) {
        LumaDrawInst none = 0;
        lt_fact("null_actor", inst->of(lt_mod, 0, &none));
        lt_fact("null_out", inst->of(lt_mod, player(), 0));
        lt_fact("of", inst->of(lt_mod, player(), &gold));
        LumaDrawInst again = 0;
        (void)inst->of(lt_mod, player(), &again);
        lt_fact("again", again == gold);
        const float red[4] = {1, 0, 0, 1};
        const float two[4] = {2, 0, 0, 1};
        lt_fact("bad_name", inst->set_material_param(lt_mod, gold, 0, "FiNoName", red));
        lt_fact("bad_value", inst->set_material_param(lt_mod, gold, 0, LUMA_DRAW_INST_PARAM_MATERIAL0, two));
        lt_fact("no_material", inst->set_material_param(lt_mod, gold, "FiNoMaterial", LUMA_DRAW_INST_PARAM_MATERIAL0, red));
        lt_fact("param", inst->set_material_param(lt_mod, gold, 0, LUMA_DRAW_INST_PARAM_MATERIAL0, red));
        lt_fact("no_part", inst->set_visible(lt_mod, gold, "FiNoJoint", 0));
        const float zero[3] = {0, 1, 1};
        lt_fact("zero_scale", inst->set_scale(lt_mod, gold, zero));
        lt_fact("texture", inst->set_texture(lt_mod, gold, 0, 0, 0));
        lt_fact("release", inst->release(lt_mod, gold));
        lt_fact("release2", inst->release(lt_mod, gold));
        lt_fact("colour_ended", inst->set_colour(lt_mod, gold, RGBA(255, 0, 0, 255)));
    }
#elif CASE == 5
    if (k == 1) lt_fact("at", draw->at(lt_mod, LUMA_DRAW_WORLD_AFTER, paint, 0));
    if (k == 2) lt_fact("off", draw->at(lt_mod, LUMA_DRAW_WORLD_AFTER, 0, 0));
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(draw, LUMA_DRAW_NAME, "1.0"),
#if CASE == 3 || CASE == 4
    LUMA_SERVICE(inst, LUMA_DRAW_INST_NAME, "1.0"),
    LUMA_FUNCTION(player_of, "MR::getPlayerDemoActor", "LiveActor *()"),
#endif
    LUMA_FUNCTION(get_player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
