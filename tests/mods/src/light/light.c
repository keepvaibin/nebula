/* FB's draw.light conformance mods (plan 2.4 layer 6; native/include/luma_draw_light.h), one case each (-DCASE=n).
   Run steps call MR::getRandom(0, 2460 + k) and the hook gives 1; k 1 to 9 are the case's steps. Each call's status is
   the fact mod.<id>.<name>.
   fl-light (1) 2461: k 1 a red light (1, 0.1, 0.1), 800 cd, default radius, casting, 150 units above the player
                  ("add"); k 2 the same light blue (0.1, 0.2, 1), radius 40 ("set"); k 3 moved 300 units over the
                  player's place now ("move"); k 4 removed ("remove").
   fl-args  (2) 2462: every refusal: add with a null desc, a null out, a desc of 4 bytes (Service.StructSize), a NaN
                  position, a position past LUMA_DRAW_LIGHT_FAR, colour 1.5, candela 0, candela 6000, radius 3,
                  radius 500, flags 2; one good add ("good"); move with null, with NaN, of handle 999999; set of 999999,
                  set with candela -1; remove ("remove"), remove again, move of the removed light.
   fl-limit (3) 2463: k 1 16 adds, each LUMA_OK ("ok": how many), the 17th ("over": LUMA_E_LIMIT, Service.Limit);
                  k 2 one removed ("remove") and one added again ("again"). */
#include "../loader/lt.h"
#include <luma_draw_light.h>

#ifndef CASE
#error CASE
#endif

typedef struct Vec3 {
    float x, y, z;
} Vec3;

static const LumaDrawLightService* light;
static const LumaFunction* get_player_pos; /* Vec3f* MR::getPlayerPos() */
__attribute__((unused)) static LumaDrawLight lamp;

__attribute__((unused)) static Vec3 player(void) {
    const Vec3* at = 0;
    void* none[] = {0};
    get_player_pos->invoke(none, &at);
    Vec3 out = {0, 0, 0};
    if (at != 0) out = *at;
    return out;
}

__attribute__((unused)) static LumaDrawLightDesc desc_at(float x, float y, float z) {
    LumaDrawLightDesc d = {0};
    d.size = sizeof(d);
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.colour[0] = 1.0f;
    d.colour[1] = 0.1f;
    d.colour[2] = 0.1f;
    d.candela = 800.0f;
    d.radius = 0.0f;
    d.flags = LUMA_DRAW_LIGHT_CASTS;
    return d;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2460;
    if (k < 1 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        const Vec3 p = player();
        LumaDrawLightDesc d = desc_at(p.x, p.y + 150.0f, p.z);
        lt_fact("add", light->add(lt_mod, &d, &lamp));
    }
    if (k == 2) {
        const Vec3 p = player();
        LumaDrawLightDesc d = desc_at(p.x, p.y + 150.0f, p.z);
        d.colour[0] = 0.1f;
        d.colour[1] = 0.2f;
        d.colour[2] = 1.0f;
        d.radius = 40.0f;
        lt_fact("set", light->set(lt_mod, lamp, &d));
    }
    if (k == 3) {
        const Vec3 p = player();
        const float to[3] = {p.x, p.y + 300.0f, p.z};
        lt_fact("move", light->move(lt_mod, lamp, to));
    }
    if (k == 4) lt_fact("remove", light->remove(lt_mod, lamp));
#elif CASE == 2
    if (k == 1) {
        LumaDrawLight none = 0;
        LumaDrawLightDesc d = desc_at(0.0f, 0.0f, 0.0f);
        lt_fact("null_desc", light->add(lt_mod, 0, &none));
        lt_fact("null_out", light->add(lt_mod, &d, 0));
        d.size = 4;
        lt_fact("small", light->add(lt_mod, &d, &none));
        d = desc_at(0.0f, 0.0f, 0.0f);
        const float zero = 0.0f;
        d.position[1] = zero / zero;
        lt_fact("nan_position", light->add(lt_mod, &d, &none));
        d = desc_at(0.0f, 0.0f, 20000000.0f);
        lt_fact("far_position", light->add(lt_mod, &d, &none));
        d = desc_at(0.0f, 0.0f, 0.0f);
        d.colour[2] = 1.5f;
        lt_fact("bright_colour", light->add(lt_mod, &d, &none));
        d = desc_at(0.0f, 0.0f, 0.0f);
        d.candela = 0.0f;
        lt_fact("no_candela", light->add(lt_mod, &d, &none));
        d.candela = 6000.0f;
        lt_fact("many_candela", light->add(lt_mod, &d, &none));
        d = desc_at(0.0f, 0.0f, 0.0f);
        d.radius = 3.0f;
        lt_fact("small_radius", light->add(lt_mod, &d, &none));
        d.radius = 500.0f;
        lt_fact("large_radius", light->add(lt_mod, &d, &none));
        d = desc_at(0.0f, 0.0f, 0.0f);
        d.flags = 2u;
        lt_fact("bad_flags", light->add(lt_mod, &d, &none));
        lt_fact("none_written", none == 0);
        d = desc_at(0.0f, 0.0f, 0.0f);
        lt_fact("good", light->add(lt_mod, &d, &lamp));
        const float nan3[3] = {0.0f, zero / zero, 0.0f};
        const float ok3[3] = {1.0f, 2.0f, 3.0f};
        lt_fact("move_null", light->move(lt_mod, lamp, 0));
        lt_fact("move_nan", light->move(lt_mod, lamp, nan3));
        lt_fact("move_unknown", light->move(lt_mod, 999999, ok3));
        lt_fact("set_unknown", light->set(lt_mod, 999999, &d));
        d.candela = -1.0f;
        lt_fact("set_bad", light->set(lt_mod, lamp, &d));
        lt_fact("remove", light->remove(lt_mod, lamp));
        lt_fact("remove2", light->remove(lt_mod, lamp));
        lt_fact("move_removed", light->move(lt_mod, lamp, ok3));
    }
#elif CASE == 3
    static LumaDrawLight all[17];
    if (k == 1) {
        int32_t ok = 0;
        for (int i = 0; i < 16; i++) {
            LumaDrawLightDesc d = desc_at((float)i * 100.0f, 0.0f, 0.0f);
            ok += light->add(lt_mod, &d, &all[i]) == LUMA_OK;
        }
        lt_fact("ok", ok);
        LumaDrawLightDesc d = desc_at(0.0f, 0.0f, 0.0f);
        lt_fact("over", light->add(lt_mod, &d, &all[16]));
    }
    if (k == 2) {
        lt_fact("remove", light->remove(lt_mod, all[0]));
        LumaDrawLightDesc d = desc_at(0.0f, 0.0f, 0.0f);
        lt_fact("again", light->add(lt_mod, &d, &all[0]));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(light, LUMA_DRAW_LIGHT_NAME, "1.0"),
    LUMA_FUNCTION(get_player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
