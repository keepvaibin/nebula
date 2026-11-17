/* J's anim conformance mods (contract animation 7; native/include/luma_anim.h), one case each (-DCASE=n). Run steps
   call MR::getRandom(0, 3600 + k) and the hook gives 1; k 1 to 6 are the case's steps. Each call's status is the fact
   mod.<id>.<name>. A play's events come to on_event with user = which play of the case (1, 2): facts ev<n>_mark<i>
   (how many times mark i came), ev<n>_end_status, ev<n>_end_why, ev<n>_ends.
   The model <mod>:sway is make_sway.py's cube (layers/base/assets/sway.gltf): Nod (once, 30 frames), Sway (loop, 60),
   Glow (loop, 60: its colour only, a .bpk). It is attached at the player's base, 150 up.
   ja-once    (1) 3611: k 1 attach ("attach"), play_on_model Nod as asked once, marks 0, 10, 20, 999 ("play").
   ja-loop    (2) 3612: k 1 attach, play Sway (as the clip: loop) at rate 2, mark 30 ("play"); k 2 frame ("frame",
                   "last", "inside": 0 <= frame <= last); k 3 stop ("stop"); k 4 stop again ("stop2"), frame ("frame2").
   ja-glow    (3) 3613: k 1 attach, play Glow, how null ("play"); k 2 play Nod from frame 10, no blend ("nod": Glow, a
                   colour clip, goes on); k 3 frame of Glow ("glow_frame").
   ja-args    (4) 3614: k 1 the refusals: a null actor, bytes that are no actor, an empty clip, rate -1, rate 17,
                   blend -2, loop 3, start -1, 17 marks, marks null with a count, a null out, a LumaAnimHow of 4 bytes,
                   start past the clip's last frame, model 999999, stop and frame of play 999999; then JaNoClip
                   ("missing": LUMA_E_NOT_FOUND, Content.MissingAnimation); then one good play ("good").
   ja-detach  (5) 3615: k 1 attach, play Sway ("play"); k 2 detach the model ("detach"): the play ends "actor".
   ja-again   (6) 3616: k 1 attach, play Sway ("play"); k 2 play Nod on it ("nod"): play 1 ends "replaced".
   ja-goomba  (7) 3617, 3618: the first Goomba (Kuribo) the game animates alive is the actor: k 1 play its Walk once
                   ("seen", "play"). 3618 runs it with ja-walk90, G's ga-walk stretched to 90 frames.
   ja-events  (8) 3619 (anim 1.1): a far Goomba; k 1 takes its clipping off ("seen"), k 2 plays its Walk once
                   ("play") with its own mark at frame 5; its layer gives Kuribo's Walk the event
                   rows start 0, step_left 6, step_right 12 (Walk.anim.json, cooked to Walk.events). A mark with a name
                   gives facts ev1_namelen<i> (its bytes) and ev1_namesum<i> (their sum).
   ja-param   (9) 3401 (content.model 1.1 set_param, plan 3.4): k 1 attach the cube, then the refusals: a null material
                   ("null_material"), a null param ("null_param"), null rgba ("null_rgba"), a NaN ("nan"), handle 999999
                   ("bad_handle"), a parameter the cube has none of ("missing": LUMA_E_NOT_FOUND, Content.MissingParam);
                   k 2 detach, then set_param on it ("ended"). */
#include "../loader/lt.h"
#include <luma_anim.h>
#include <luma_content_model.h>
#include <stdint.h>

#ifndef CASE
#error CASE
#endif

static const LumaAnimService* an;
static const LumaContentModelService* md;
static const LumaFunction* player_of; /* LiveActor* MR::getPlayerDemoActor() */
__attribute__((unused)) static LumaContentModel model;
__attribute__((unused)) static LumaAnimPlay play1, play2;
static long long marks[3][LUMA_ANIM_MOST_MARKS];
static long long ends[3];

__attribute__((unused)) static LumaActor player(void) {
    void* actor = 0;
    void* args[] = {0};
    player_of->invoke(args, &actor);
    return actor;
}

/* fact ev<who>_<what>[<i>] <v> */
__attribute__((unused)) static void ev_fact(int who, const char* what, int i, long long v) {
    char key[40];
    char* at = lt_put(key, "ev");
    at = lt_num(at, who);
    *at++ = '_';
    at = lt_put(at, what);
    if (i >= 0) at = lt_num(at, i);
    *at = 0;
    lt_fact(key, v);
}

__attribute__((unused)) static void on_event(void* user, const LumaEvent* event) {
    const int who = (int)(intptr_t)user;
    if (who < 1 || who > 2) return;
    if (event->kind == LUMA_ANIM_EVENT_MARK && event->value < LUMA_ANIM_MOST_MARKS) {
        ev_fact(who, "mark", (int)event->value, ++marks[who][event->value]);
        if (event->data != 0 && event->data_size != 0) {
            long long sum = 0;
            for (uint64_t i = 0; i < event->data_size; i++) sum += ((const unsigned char*)event->data)[i];
            ev_fact(who, "namelen", (int)event->value, (long long)event->data_size);
            ev_fact(who, "namesum", (int)event->value, sum);
        }
    } else if (event->kind == LUMA_ANIM_EVENT_END) {
        ev_fact(who, "end_status", -1, event->status);
        ev_fact(who, "end_why", -1, (long long)event->value);
        ev_fact(who, "ends", -1, ++ends[who]);
    }
}

/* The mod's own model, named as a literal (the validator lifts model names from the code: references 5). */
#if CASE == 1
#define MODEL "ja-once:sway"
#elif CASE == 2
#define MODEL "ja-loop:sway"
#elif CASE == 3
#define MODEL "ja-glow:sway"
#elif CASE == 4
#define MODEL "ja-args:sway"
#elif CASE == 5
#define MODEL "ja-detach:sway"
#elif CASE == 6
#define MODEL "ja-again:sway"
#elif CASE == 9
#define MODEL "ja-param:sway"
#endif

#ifdef MODEL
static int32_t attach(void) {
    const LumaPlace place = {.size = sizeof(LumaPlace), .offset = {0.0f, 150.0f, 0.0f}, .turn = {0.0f, 0.0f, 0.0f}};
    return md->attach(lt_mod, player(), MODEL, LUMA_CONTENT_MODEL_ACTOR, 0, &place, &model);
}
#endif

#if CASE == 7 || CASE == 8
static int32_t c_kuribo;          /* the class Kuribo */
#if CASE == 8
static const LumaFunction* near_of;  /* bool MR::isNearPlayerAnyTime(const LiveActor *, f32) */
static const LumaFunction* unclip;   /* void MR::invalidateClipping(LiveActor *) */
static const LumaFunction* player_actor; /* MarioActor *MarioAccess::getPlayerActor() (null before the stage makes him) */
#endif
static int32_t flag_at, dead_at;  /* LiveActor.mFlag, LiveActorFlag.mIsDead (MR::isDead is inline: no function) */
static void* goomba;              /* set once, by the game thread's calcAnim */

static bool is_dead(const void* actor) {
    return *(const bool*)((const char*)actor + flag_at + dead_at);
}

/* CASE 8 (marks and events over a whole clip) takes a Goomba far from the player: the game's "non active" one
   (Kuribo::exeNonActive starts no clip of its own until the player is within 3000), so its Walk is the mod's for all its
   frames. The game moves no frame of a clipped actor (it is off the scene until the clip ends) and the first Goomba the
   game animates is usually such a far one, held by the clipping from the next frame on: the play takes its clipping off. */
static int32_t look(LumaCall* call, void* user) {
    (void)user;
    void* const actor = call->args[0];
    if (__atomic_load_n(&goomba, __ATOMIC_ACQUIRE) == 0 && lt_core->is_a(actor, c_kuribo) == 1 && !is_dead(actor)) {
#if CASE == 8
        void* player_it = 0;
        void* none_args[] = {0};
        player_actor->invoke(none_args, &player_it);
        if (player_it == 0) return 0;  /* MR::isNearPlayerAnyTime reads his position */
        unsigned char near = 1;
        const float radius = 3000.0f;
        void* near_args[] = {0, (void*)&actor, (void*)&radius};
        near_of->invoke(near_args, &near);
        if (near != 0) return 0;
#endif
        void* none = 0;
        __atomic_compare_exchange_n(&goomba, &none, actor, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    return 0;
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3600;
    if (k < 1 || k > 6) return 0;
#if CASE == 1
    if (k == 1) {
        static const float at[4] = {0.0f, 10.0f, 20.0f, 999.0f};
        const LumaAnimHow how = {.size = sizeof(LumaAnimHow), .loop = LUMA_ANIM_ONCE, .mark_count = 4, .marks = at};
        lt_fact("attach", attach());
        lt_fact("play", an->play_on_model(lt_mod, model, "Nod", &how, on_event, (void*)1, &play1));
    }
#elif CASE == 2
    if (k == 1) {
        static const float at[1] = {30.0f};
        const LumaAnimHow how = {.size = sizeof(LumaAnimHow), .rate = 2.0f, .mark_count = 1, .marks = at};
        lt_fact("attach", attach());
        lt_fact("play", an->play_on_model(lt_mod, model, "Sway", &how, on_event, (void*)1, &play1));
    }
    if (k == 2) {
        float frame = -1.0f, last = -1.0f;
        lt_fact("frame", an->frame(lt_mod, play1, &frame, &last));
        lt_fact("last", (long long)last);
        lt_fact("inside", frame >= 0.0f && frame <= last);
    }
    if (k == 3) lt_fact("stop", an->stop(lt_mod, play1));
    if (k == 4) {
        lt_fact("stop2", an->stop(lt_mod, play1));
        lt_fact("frame2", an->frame(lt_mod, play1, 0, 0));
    }
#elif CASE == 3
    if (k == 1) {
        lt_fact("attach", attach());
        lt_fact("play", an->play_on_model(lt_mod, model, "Glow", 0, on_event, (void*)1, &play1));
    }
    if (k == 2) {
        const LumaAnimHow how = {.size = sizeof(LumaAnimHow), .blend = LUMA_ANIM_BLEND_NONE, .start = 10.0f};
        lt_fact("nod", an->play_on_model(lt_mod, model, "Nod", &how, on_event, (void*)2, &play2));
    }
    if (k == 3) {
        float frame = -1.0f;
        lt_fact("glow_frame", an->frame(lt_mod, play1, &frame, 0));
    }
#elif CASE == 4
    if (k == 1) {
        static const char not_actor[64] = {0};
        static const float many[LUMA_ANIM_MOST_MARKS + 1] = {0};
        LumaAnimPlay none = 0;
        lt_fact("attach", attach());
        const LumaAnimHow rate_low = {.size = sizeof(LumaAnimHow), .rate = -1.0f};
        const LumaAnimHow rate_high = {.size = sizeof(LumaAnimHow), .rate = 17.0f};
        const LumaAnimHow blend = {.size = sizeof(LumaAnimHow), .blend = -2};
        const LumaAnimHow loop = {.size = sizeof(LumaAnimHow), .loop = 3};
        const LumaAnimHow start = {.size = sizeof(LumaAnimHow), .start = -1.0f};
        const LumaAnimHow too_many = {.size = sizeof(LumaAnimHow), .mark_count = LUMA_ANIM_MOST_MARKS + 1, .marks = many};
        const LumaAnimHow null_marks = {.size = sizeof(LumaAnimHow), .mark_count = 1};
        const LumaAnimHow small = {.size = 4};
        const LumaAnimHow past = {.size = sizeof(LumaAnimHow), .start = 1000.0f};
        lt_fact("null_actor", an->play(lt_mod, 0, "Nod", 0, 0, 0, &none));
        lt_fact("not_actor", an->play(lt_mod, (LumaActor)not_actor, "Nod", 0, 0, 0, &none));
        lt_fact("empty_clip", an->play_on_model(lt_mod, model, "", 0, 0, 0, &none));
        lt_fact("rate_low", an->play_on_model(lt_mod, model, "Nod", &rate_low, 0, 0, &none));
        lt_fact("rate_high", an->play_on_model(lt_mod, model, "Nod", &rate_high, 0, 0, &none));
        lt_fact("blend", an->play_on_model(lt_mod, model, "Nod", &blend, 0, 0, &none));
        lt_fact("loop", an->play_on_model(lt_mod, model, "Nod", &loop, 0, 0, &none));
        lt_fact("start", an->play_on_model(lt_mod, model, "Nod", &start, 0, 0, &none));
        lt_fact("too_many", an->play_on_model(lt_mod, model, "Nod", &too_many, 0, 0, &none));
        lt_fact("null_marks", an->play_on_model(lt_mod, model, "Nod", &null_marks, 0, 0, &none));
        lt_fact("null_out", an->play_on_model(lt_mod, model, "Nod", 0, 0, 0, 0));
        lt_fact("small_how", an->play_on_model(lt_mod, model, "Nod", &small, 0, 0, &none));
        lt_fact("past", an->play_on_model(lt_mod, model, "Nod", &past, 0, 0, &none));
        lt_fact("model_unknown", an->play_on_model(lt_mod, 999999, "Nod", 0, 0, 0, &none));
        lt_fact("stop_unknown", an->stop(lt_mod, 999999));
        lt_fact("frame_unknown", an->frame(lt_mod, 999999, 0, 0));
        lt_fact("missing", an->play_on_model(lt_mod, model, "JaNoClip", 0, 0, 0, &none));
        lt_fact("none_written", none == 0);
        lt_fact("good", an->play_on_model(lt_mod, model, "Nod", 0, 0, 0, &play1));
    }
#elif CASE == 5
    if (k == 1) {
        lt_fact("attach", attach());
        lt_fact("play", an->play_on_model(lt_mod, model, "Sway", 0, on_event, (void*)1, &play1));
    }
    if (k == 2) lt_fact("detach", md->detach(lt_mod, model));
#elif CASE == 6
    if (k == 1) {
        lt_fact("attach", attach());
        lt_fact("play", an->play_on_model(lt_mod, model, "Sway", 0, on_event, (void*)1, &play1));
    }
    if (k == 2) lt_fact("nod", an->play_on_model(lt_mod, model, "Nod", 0, on_event, (void*)2, &play2));
#elif CASE == 7
    void* const it = __atomic_load_n(&goomba, __ATOMIC_ACQUIRE);
    if (k == 1) {
        const LumaAnimHow how = {.size = sizeof(LumaAnimHow), .loop = LUMA_ANIM_ONCE};
        lt_fact("seen", it != 0);
        if (it != 0) lt_fact("play", an->play(lt_mod, it, "Walk", &how, on_event, (void*)1, &play1));
    }
#elif CASE == 9
    static const float one[4] = {1.0f, 0.5f, 0.25f, 1.0f};
    if (k == 1) {
        float nan[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        nan[1] = __builtin_nanf("");
        lt_fact("attach", attach());
        lt_fact("null_material", md->set_param(lt_mod, model, 0, "stripes", one));
        lt_fact("null_param", md->set_param(lt_mod, model, "sway", 0, one));
        lt_fact("null_rgba", md->set_param(lt_mod, model, "sway", "stripes", 0));
        lt_fact("nan", md->set_param(lt_mod, model, "sway", "stripes", nan));
        lt_fact("bad_handle", md->set_param(lt_mod, 999999, "sway", "stripes", one));
        lt_fact("missing", md->set_param(lt_mod, model, "sway", "stripes", one));
    }
    if (k == 2) {
        lt_fact("detach", md->detach(lt_mod, model));
        lt_fact("ended", md->set_param(lt_mod, model, "sway", "stripes", one));
    }
#elif CASE == 8
    /* k 1 takes the Goomba's clipping off; k 2, some frames on, plays. Unclipped, a Goomba the clipping held before it
       went non active runs its wander a frame or two (its own Wait or Walk) before tryNonActive stops it: a play started
       with the unclip would be replaced by it. */
    void* const it = __atomic_load_n(&goomba, __ATOMIC_ACQUIRE);
    if (k == 1) {
        lt_fact("seen", it != 0);
        if (it != 0) {
            void* unclip_args[] = {0, (void*)&it};
            unclip->invoke(unclip_args, 0);
        }
    }
    if (k == 2 && it != 0) {
        static const float at[1] = {5.0f};
        const LumaAnimHow how = {.size = sizeof(LumaAnimHow), .loop = LUMA_ANIM_ONCE, .mark_count = 1, .marks = at};
        lt_fact("play", an->play(lt_mod, it, "Walk", &how, on_event, (void*)1, &play1));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 8
    LUMA_SERVICE(an, LUMA_ANIM_NAME, "1.1"),
#else
    LUMA_SERVICE(an, LUMA_ANIM_NAME, "1.0"),
#endif
#if CASE == 9
    LUMA_SERVICE(md, LUMA_CONTENT_MODEL_NAME, "1.1"),
#else
    LUMA_SERVICE(md, LUMA_CONTENT_MODEL_NAME, "1.0"),
#endif
    LUMA_FUNCTION(player_of, "MR::getPlayerDemoActor", "LiveActor *()"),
#if CASE == 7 || CASE == 8
    LUMA_CLASS(c_kuribo, "Kuribo"),
#if CASE == 8
    LUMA_FUNCTION(near_of, "MR::isNearPlayerAnyTime", "bool(const LiveActor *, f32)"),
    LUMA_FUNCTION(unclip, "MR::invalidateClipping", "void(LiveActor *)"),
    LUMA_FUNCTION(player_actor, "MarioAccess::getPlayerActor", "MarioActor *()"),
#endif
    LUMA_FIELD(flag_at, "LiveActor.mFlag", "LiveActorFlag"),
    LUMA_FIELD(dead_at, "LiveActorFlag.mIsDead", "bool"),
    LUMA_HOOK_ON("LiveActor::calcAnim", "void()", LUMA_AFTER, look),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
