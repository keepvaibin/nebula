/* J's content.fx conformance mods (contract effects, section 3; native/include/luma_content_fx.h), one case each
   (-DCASE=n). Run steps call MR::getRandom(0, 3500 + k) and the hook gives 1; k 1 to 6 are the case's steps. Each call's
   status is the fact mod.<id>.<name>. The effect is the player's (MR::getPlayerDemoActor): TicoTouchGlow00 is one
   particle of the game's ParticleData (numbered, so it names itself); JeNoSuchParticle00 is in no store.
   je-fx-hand    (1) 3501: k 1 start TicoTouchGlow00 at HandR, followed (status "start"); k 2 stop, fading
                      ("stop"); k 3 stop again ("stop2": the handle ended).
   je-fx-missing (2) 3502: k 1 and k 2 start JeNoSuchParticle00 ("start1", "start2").
   je-fx-joint   (3) 3503: k 1 start TicoTouchGlow00 at JeNoJoint ("start").
   je-fx-args    (4) 3504: k 1 the refusals: a null actor, an empty name, a null out, a LumaPlace of 4 bytes, an actor
                      that is not one (the mod's own bytes), start_on_model (no model handles yet), stop and tint of
                      handle 999999; then one good start ("good"), tint scale 0 and a NaN scale on it, stop now.
   je-fx-tint    (5) 3505: k 1 start TicoTouchGlow00 at the base, placed (offset 0 120 0, turn 0 90 0), left where it
                      started; k 2 tint 2.0 and 0xFF8000FF ("tint"); k 3 stop now ("stop").
   je-fx-keep    (6) 3506: k 1 start TicoTouchGlow00 at Spine1, followed; never stopped: it ends with the scene when
                      the harness ends the run, or lives to the end ("start").
   je-fx-model   (7) 3510: k 1 attach the game's Coin 150 up from the player's base (content.model, "attach"), start
                      TicoTouchGlow00 on it at its root ("start"), and at JeNoSocket ("socket"); k 2 detach the model
                      ("detach"): both effects end "model"; k 3 start_on_model on the ended handle ("ended").
   je-fx-limit   (8) 3512: k 1 256 starts on the player ("ok" the count of LUMA_OK), the 257th ("over"); k 2 stop one
                      ("stop") and start again ("again").
   je-fx-dies    (9) 3513: the first Goomba (Kuribo) the game animates alive (LiveActor::calcAnim, after) is the actor:
                      k 1 start TicoTouchGlow00 on it, followed ("seen", "start"); k 2 its own kill through the vtable
                      (call_method "LiveActor::kill", as a stomp ends in): the handle ends "actor"; k 3 stop the ended
                      handle ("stop") and start on the dead Goomba ("dead"). */
#include "../loader/lt.h"
#include <luma_content_fx.h>
#include <luma_content_model.h>

#ifndef CASE
#error CASE
#endif

static const LumaContentFxService* fx;
__attribute__((unused)) static const LumaContentModelService* md;
__attribute__((unused)) static LumaContentModel model;
static const LumaFunction* player_of; /* LiveActor* MR::getPlayerDemoActor() */
__attribute__((unused)) static LumaContentFxEffect effect;
#if CASE == 9
static int32_t c_kuribo;              /* the class Kuribo */
static int32_t flag_at, dead_at;      /* LiveActor.mFlag, LiveActorFlag.mIsDead (MR::isDead is inline: no function) */
static void* goomba;                  /* set once, by the game thread's calcAnim */

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

__attribute__((unused)) static LumaActor player(void) {
    void* actor = 0;
    void* args[] = {0};
    player_of->invoke(args, &actor);
    return actor;
}

#define GLOW "TicoTouchGlow00"

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3500;
    if (k < 1 || k > 6) return 0;
#if CASE == 1
    if (k == 1) lt_fact("start", fx->start(lt_mod, player(), GLOW, "HandR", 0, true, &effect));
    if (k == 2) lt_fact("stop", fx->stop(lt_mod, effect, false));
    if (k == 3) lt_fact("stop2", fx->stop(lt_mod, effect, false));
#elif CASE == 2
    if (k == 1) lt_fact("start1", fx->start(lt_mod, player(), "JeNoSuchParticle00", 0, 0, true, &effect));
    if (k == 2) lt_fact("start2", fx->start(lt_mod, player(), "JeNoSuchParticle00", 0, 0, true, &effect));
#elif CASE == 3
    if (k == 1) lt_fact("start", fx->start(lt_mod, player(), GLOW, "JeNoJoint", 0, true, &effect));
#elif CASE == 4
    if (k == 1) {
        static const char not_actor[64] = {0};
        const LumaPlace small = {.size = 4};
        LumaContentFxEffect none = 0;
        lt_fact("null_actor", fx->start(lt_mod, 0, GLOW, 0, 0, true, &none));
        lt_fact("empty_name", fx->start(lt_mod, player(), "", 0, 0, true, &none));
        lt_fact("null_out", fx->start(lt_mod, player(), GLOW, 0, 0, true, 0));
        lt_fact("small_place", fx->start(lt_mod, player(), GLOW, 0, &small, true, &none));
        lt_fact("not_actor", fx->start(lt_mod, (LumaActor)not_actor, GLOW, 0, 0, true, &none));
        lt_fact("on_model", fx->start_on_model(lt_mod, 1, GLOW, 0, 0, true, &none));
        lt_fact("stop_unknown", fx->stop(lt_mod, 999999, false));
        lt_fact("tint_unknown", fx->tint(lt_mod, 999999, 1.0f, 0xFFFFFFFFu));
        lt_fact("none_written", none == 0);
        lt_fact("good", fx->start(lt_mod, player(), GLOW, 0, 0, true, &effect));
        lt_fact("tint_zero", fx->tint(lt_mod, effect, 0.0f, 0xFFFFFFFFu));
        lt_fact("tint_nan", fx->tint(lt_mod, effect, __builtin_nanf(""), 0xFFFFFFFFu));
        lt_fact("stop_now", fx->stop(lt_mod, effect, true));
    }
#elif CASE == 5
    if (k == 1) {
        const LumaPlace place = {.size = sizeof(LumaPlace), .offset = {0.0f, 120.0f, 0.0f}, .turn = {0.0f, 90.0f, 0.0f}};
        lt_fact("start", fx->start(lt_mod, player(), GLOW, 0, &place, false, &effect));
    }
    if (k == 2) lt_fact("tint", fx->tint(lt_mod, effect, 2.0f, 0xFF8000FFu));
    if (k == 3) lt_fact("stop", fx->stop(lt_mod, effect, true));
#elif CASE == 6
    if (k == 1) lt_fact("start", fx->start(lt_mod, player(), GLOW, "Spine1", 0, true, &effect));
#elif CASE == 7
    if (k == 1) {
        const LumaPlace place = {.size = sizeof(LumaPlace), .offset = {0.0f, 150.0f, 0.0f}, .turn = {0.0f, 0.0f, 0.0f}};
        LumaContentFxEffect second = 0;
        lt_fact("attach", md->attach(lt_mod, player(), "Coin", LUMA_CONTENT_MODEL_ACTOR, 0, &place, &model));
        lt_fact("start", fx->start_on_model(lt_mod, model, GLOW, 0, 0, true, &effect));
        lt_fact("socket", fx->start_on_model(lt_mod, model, GLOW, "JeNoSocket", 0, true, &second));
    }
    if (k == 2) lt_fact("detach", md->detach(lt_mod, model));
    if (k == 3) {
        LumaContentFxEffect none = 0;
        lt_fact("ended", fx->start_on_model(lt_mod, model, GLOW, 0, 0, true, &none));
    }
#elif CASE == 8
    static LumaContentFxEffect all[LUMA_CONTENT_FX_MOST];
    if (k == 1) {
        int32_t ok = 0;
        for (uint32_t i = 0; i < LUMA_CONTENT_FX_MOST; i++) {
            ok += fx->start(lt_mod, player(), GLOW, 0, 0, true, &all[i]) == LUMA_OK;
        }
        LumaContentFxEffect over = 0;
        lt_fact("ok", ok);
        lt_fact("over", fx->start(lt_mod, player(), GLOW, 0, 0, true, &over));
        lt_fact("over_written", over != 0);
    }
    if (k == 2) {
        lt_fact("stop", fx->stop(lt_mod, all[0], true));
        lt_fact("again", fx->start(lt_mod, player(), GLOW, 0, 0, true, &all[0]));
    }
#elif CASE == 9
    void* const it = __atomic_load_n(&goomba, __ATOMIC_ACQUIRE);
    if (k == 1) {
        lt_fact("seen", it != 0);
        if (it != 0) lt_fact("start", fx->start(lt_mod, it, GLOW, 0, 0, true, &effect));
    }
    if (k == 2 && it != 0) lt_fact("kill", lt_core->call_method(lt_mod, it, "LiveActor::kill", "void()", 0, 0));
    if (k == 3 && it != 0) {
        LumaContentFxEffect none = 0;
        lt_fact("stop", fx->stop(lt_mod, effect, false));
        lt_fact("dead", fx->start(lt_mod, it, GLOW, 0, 0, true, &none));
        lt_fact("none_written", none == 0);
    }
#elif CASE == 10
    /* 3509: the mod's own effect je-fx-group:trio (layers/base/assets/effects.json: three emitters, draw
       AFTER_INDIRECT), on the player's base. */
    if (k == 1) lt_fact("start", fx->start(lt_mod, player(), "je-fx-group:trio", 0, 0, true, &effect));
    if (k == 2) lt_fact("stop", fx->stop(lt_mod, effect, false));
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(fx, LUMA_CONTENT_FX_NAME, "1.0"),
#if CASE == 7
    LUMA_SERVICE(md, LUMA_CONTENT_MODEL_NAME, "1.0"),
#endif
#if CASE == 9
    LUMA_CLASS(c_kuribo, "Kuribo"),
    LUMA_FIELD(flag_at, "LiveActor.mFlag", "LiveActorFlag"),
    LUMA_FIELD(dead_at, "LiveActorFlag.mIsDead", "bool"),
    LUMA_HOOK_ON("LiveActor::calcAnim", "void()", LUMA_AFTER, look),
#endif
    LUMA_FUNCTION(player_of, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
