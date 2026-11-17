/* J's content.model conformance mods (contract models, section 7; native/include/luma_content_model.h), one case each
   (-DCASE=n). Run steps call MR::getRandom(0, 3200 + k) and the hook gives 1; k 1 to 6 are the case's steps. Each
   call's status is the fact mod.<id>.<name>. The host is the player (MR::getPlayerDemoActor). Coin is a model of the
   game's ObjectData; Luigi has the player's skeleton (the same joint names); JmNoSuchModel is in no store.
   jm-hand     (1) 3201: k 1 attach Coin at the joint HandR ("attach").
   jm-missing  (2) 3202: k 1 and k 2 attach JmNoSuchModel at the base ("attach1", "attach2").
   jm-joint    (3) 3203: k 1 attach Coin at JmNoJoint, a joint the player lacks ("attach").
   jm-args     (4) 3204: k 1 the refusals: a null actor, an empty name, a follow of 3, a null out, a LumaPlace of 4
                    bytes, an actor that is not one (the mod's own bytes), detach and show of handle 999999, player_parts
                    with a null parts and a count; then one good attach ("good") and its detach ("detach").
   jm-skeleton (5) 3205: k 1 attach Luigi following the player's skeleton ("attach").
   jm-show     (6) 3206: k 1 attach Coin at the base, 200 up ("attach"); k 2 hide ("hide"); k 3 show ("show"); k 4
                    detach ("detach"); k 5 detach again ("detach2": ended); k 6 attach it again ("again": the dead part
                    used again).
   jm-parts    (7) 3207: k 1 player_parts with no room ("none") and the total ("total"); with room for 2 ("two"),
                    the second's joint "HandR" ("second_hand_r": 1).
   jm-limit    (8) 3210: k 1 64 attaches of Coin at the base, each LUMA_OK ("ok": how many), the 65th ("over"); k 2
                    one detach ("detach") and one attach again ("again": LUMA_OK, a slot free).
   jm-dies     (9) 3209: the first Goomba (Kuribo) the game animates alive (LiveActor::calcAnim, after) is the host:
                    k 1 attach Coin at its base, 100 up ("seen", "attach"); k 2 its own kill through the vtable
                    (call_method "LiveActor::kill", as a stomp ends in): the handle ends "actor"; k 3 detach the ended
                    handle ("detach") and attach on the dead Goomba ("dead").
   jm-metre   (10) 3208: k 1 attach the mod's own model jm-metre:metre (layers/base/assets/metre.gltf, G's 1 m model:
                    joint 0 one metre up) at the player's base ("attach").
   jm-levels  (11) 3212: the mod's model jm-levels:lv and its level lvLow (make_levels.py: a red and a blue 1 m cube):
                    k 1 attach lv at the player's base ("attach"); k 2 attach the level block jm-levels:lvLow
                    ("level": LUMA_E_ARGUMENT, "none_written": 1).
   jm-lod     (12) 3213, 3214 (FactoryGalaxy 2): the host is AsteroidBlockPlanet (Toy Time's 8-bit planet, view group 26,
                    with its Low archive), found when the game makes its LodCtrl's far model (LodCtrl::createFarModel)
                    by that far model's name (asteroidblockplanetLow, case ignored; the planet's own name is Japanese).
                    k 1 attach jm-lod:lv (make_levels.py:
                    red base, blue lvLow) at its base ("seen", "attach"); k 2 the player held each frame in group 26's
                    far cube (FactoryGalaxy ViewGroupCtrlCube l_id 59, Obj_arg1 3: centre 29911.94, 9779.62; y 39400 is
                    inside it whether its origin is its base or its centre); k 3 held in group 26's near cube (l_id 54,
                    31000, 11500). */
#include "../loader/lt.h"
#include <luma_content_model.h>

#ifndef CASE
#error CASE
#endif

static const LumaContentModelService* md;
static const LumaFunction* player_of; /* LiveActor* MR::getPlayerDemoActor() */
__attribute__((unused)) static LumaContentModel model;

__attribute__((unused)) static LumaActor player(void) {
    void* actor = 0;
    void* args[] = {0};
    player_of->invoke(args, &actor);
    return actor;
}

#if CASE == 9
static int32_t c_kuribo;          /* the class Kuribo */
static int32_t flag_at, dead_at;  /* LiveActor.mFlag, LiveActorFlag.mIsDead (MR::isDead is inline: no function) */
static void* goomba;              /* set once, by the game thread's calcAnim */

static bool is_dead(const void* actor) {
    return *(const bool*)((const char*)actor + flag_at + dead_at);
}

static int32_t look(LumaCall* call, void* user) {
    (void)user;
    void* const actor = call->args[0];
    if (__atomic_load_n(&goomba, __ATOMIC_ACQUIRE) == 0 && lt_core->is_a(actor, c_kuribo) == 1 && !is_dead(actor)) {
        void* none = 0;
        __atomic_compare_exchange_n(&goomba, &none, actor, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    return 0;
}
#endif

__attribute__((unused)) static int same(const char* a, const char* b) {
    while (*a != 0 && *a == *b) a++, b++;
    return *a == *b;
}

#if CASE == 12
static int32_t name_at;              /* NameObj.mName */
static int32_t lod_actor_at;         /* LodCtrl.mActor */
static int32_t far_model_at;         /* LodCtrl.mFarModel */
static const LumaFunction* set_pos;  /* void MR::setPlayerPos(const Vec3f &) */
static void* planet;                 /* set once, when the game makes its far model */
static int hold;                     /* 0 none, 1 the far cube, 2 the near cube */

/* ASCII case ignored, as the game compares archive names (MR::isEqualStringCase). */
static int same_case(const char* a, const char* b) {
    for (;; a++, b++) {
        const char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        const char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return 0;
        if (x == 0) return 1;
    }
}

/* The planet's own name (NameObj.mName) is the Japanese one the placement gives its maker; its far model is named
   after its archive and "Low" (the archive's name as the store keeps it, in lower case): "asteroidblockplanetLow". */
static int32_t look(LumaCall* call, void* user) {
    (void)user;
    void* const actor = *(void* const*)((const char*)call->args[0] + lod_actor_at);
    void* const far = *(void* const*)((const char*)call->args[0] + far_model_at);
    const char* const name = far != 0 ? *(const char* const*)((const char*)far + name_at) : 0;
    if (__atomic_load_n(&planet, __ATOMIC_ACQUIRE) == 0 && name != 0 && same_case(name, "AsteroidBlockPlanetLow")) {
        void* none = 0;
        __atomic_compare_exchange_n(&planet, &none, actor, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    return 0;
}

static int32_t every_frame(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    static const float far_cube[3] = {29911.94f, 39400.0f, 9779.62f};
    static const float near_cube[3] = {31000.0f, 39400.0f, 11500.0f};
    if (hold != 0) {
        float at[3];
        const float* const from = hold == 1 ? far_cube : near_cube;
        at[0] = from[0], at[1] = from[1], at[2] = from[2];
        void* args[2] = {0, at};
        set_pos->invoke(args, 0);
    }
    return 0;
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3200;
    if (k < 1 || k > 6) return 0;
#if CASE == 1
    if (k == 1) lt_fact("attach", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_JOINT, "HandR", 0, &model));
#elif CASE == 2
    if (k == 1) lt_fact("attach1", md->attach(lt_mod, player(), "JmNoSuchModel", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
    if (k == 2) lt_fact("attach2", md->attach(lt_mod, player(), "JmNoSuchModel", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
#elif CASE == 3
    if (k == 1) lt_fact("attach", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_JOINT, "JmNoJoint", 0, &model));
#elif CASE == 4
    if (k == 1) {
        static const char not_actor[64] = {0};
        const LumaPlace small = {.size = 4};
        LumaContentModel none = 0;
        uint32_t total = 0;
        lt_fact("null_actor", md->attach(lt_mod, 0, "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &none));
        lt_fact("empty_name", md->attach(lt_mod, player(), "", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &none));
        lt_fact("bad_follow", md->attach(lt_mod, player(), "Coin", (LumaContentModelFollow)3, 0, 0, &none));
        lt_fact("null_out", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, 0));
        lt_fact("small_place", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, &small, &none));
        lt_fact("not_actor", md->attach(lt_mod, (LumaActor)not_actor, "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &none));
        lt_fact("detach_unknown", md->detach(lt_mod, 999999));
        lt_fact("show_unknown", md->show(lt_mod, 999999, true));
        lt_fact("parts_null", md->player_parts(lt_mod, 0, 1, &total));
        lt_fact("none_written", none == 0);
        lt_fact("good", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
        lt_fact("detach", md->detach(lt_mod, model));
    }
#elif CASE == 5
    if (k == 1) lt_fact("attach", md->attach(lt_mod, player(), "Luigi", LUMA_CONTENT_MODEL_SKELETON, 0, 0, &model));
#elif CASE == 6
    const LumaPlace place = {.size = sizeof(LumaPlace), .offset = {0.0f, 200.0f, 0.0f}, .turn = {0.0f, 0.0f, 0.0f}};
    if (k == 1) lt_fact("attach", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, &place, &model));
    if (k == 2) lt_fact("hide", md->show(lt_mod, model, false));
    if (k == 3) lt_fact("show", md->show(lt_mod, model, true));
    if (k == 4) lt_fact("detach", md->detach(lt_mod, model));
    if (k == 5) lt_fact("detach2", md->detach(lt_mod, model));
    if (k == 6) lt_fact("again", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, &place, &model));
#elif CASE == 7
    if (k == 1) {
        uint32_t total = 0;
        LumaPlayerPart parts[2] = {{.size = sizeof(LumaPlayerPart)}, {.size = sizeof(LumaPlayerPart)}};
        lt_fact("none", md->player_parts(lt_mod, 0, 0, &total));
        lt_fact("total", total);
        lt_fact("two", md->player_parts(lt_mod, parts, 2, 0));
        lt_fact("second_hand_r", parts[1].joint != 0 && same(parts[1].joint, "HandR"));
    }
#elif CASE == 8
    static LumaContentModel all[LUMA_CONTENT_MODEL_MOST];
    if (k == 1) {
        int32_t ok = 0;
        for (uint32_t i = 0; i < LUMA_CONTENT_MODEL_MOST; i++) {
            ok += md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &all[i]) == LUMA_OK;
        }
        lt_fact("ok", ok);
        lt_fact("over", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
    }
    if (k == 2) {
        lt_fact("detach", md->detach(lt_mod, all[0]));
        lt_fact("again", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &all[0]));
    }
#elif CASE == 9
    void* const it = __atomic_load_n(&goomba, __ATOMIC_ACQUIRE);
    if (k == 1) {
        const LumaPlace place = {.size = sizeof(LumaPlace), .offset = {0.0f, 100.0f, 0.0f}, .turn = {0.0f, 0.0f, 0.0f}};
        lt_fact("seen", it != 0);
        if (it != 0) lt_fact("attach", md->attach(lt_mod, it, "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, &place, &model));
    }
    if (k == 2 && it != 0) lt_fact("kill", lt_core->call_method(lt_mod, it, "LiveActor::kill", "void()", 0, 0));
    if (k == 3 && it != 0) {
        LumaContentModel none = 0;
        lt_fact("detach", md->detach(lt_mod, model));
        lt_fact("dead", md->attach(lt_mod, it, "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &none));
        lt_fact("none_written", none == 0);
    }
#elif CASE == 10
    if (k == 1) lt_fact("attach", md->attach(lt_mod, player(), "jm-metre:metre", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
#elif CASE == 11
    if (k == 1) lt_fact("attach", md->attach(lt_mod, player(), "jm-levels:lv", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
    if (k == 2) {
        LumaContentModel none = 0;
        lt_fact("level", md->attach(lt_mod, player(), "jm-levels:lvLow", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &none));
        lt_fact("none_written", none == 0);
    }
#elif CASE == 12
    void* const it = __atomic_load_n(&planet, __ATOMIC_ACQUIRE);
    if (k == 1) {
        lt_fact("seen", it != 0);
        if (it != 0) lt_fact("attach", md->attach(lt_mod, it, "jm-lod:lv", LUMA_CONTENT_MODEL_ACTOR, 0, 0, &model));
    }
    if (k == 2) hold = 1;
    if (k == 3) hold = 2;
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(md, LUMA_CONTENT_MODEL_NAME, "1.0"),
    LUMA_FUNCTION(player_of, "MR::getPlayerDemoActor", "LiveActor *()"),
#if CASE == 12
    LUMA_FIELD(name_at, "NameObj.mName", "const char *"),
    LUMA_FIELD(lod_actor_at, "LodCtrl.mActor", "LiveActor *"),
    LUMA_FIELD(far_model_at, "LodCtrl.mFarModel", "ModelObj *"),
    LUMA_FUNCTION(set_pos, "MR::setPlayerPos", "void(const Vec3f &)"),
    LUMA_HOOK_ON("LodCtrl::createFarModel", "void(int, int, int)", LUMA_AFTER, look),
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_AFTER, every_frame),
#endif
#if CASE == 9
    LUMA_CLASS(c_kuribo, "Kuribo"),
    LUMA_FIELD(flag_at, "LiveActor.mFlag", "LiveActorFlag"),
    LUMA_FIELD(dead_at, "LiveActorFlag.mIsDead", "bool"),
    LUMA_HOOK_ON("LiveActor::calcAnim", "void()", LUMA_AFTER, look),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
