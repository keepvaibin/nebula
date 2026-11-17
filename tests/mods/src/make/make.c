/* F's conformance mod for LumaCore.make (contract classes "make", plan 1.3): K's ask for LumaKit's actor.

   mk-make: in start (no scene making objects) make answers -3. After StageDataHolder::initPlacementMario (the scene
   making its objects) it makes eight Kuribo at eight places (each its own pos and scale; Obj_arg0 1 for the first
   four, -1 for the rest, which Kuribo::init reads into mIsNoGravitySupport), each of class Kuribo; one more with
   LUMA_MAKE_DEAD, dead; and refuses an unknown name (-1), a count of 0 (-4) and an unknown flag (-4). At the harness's
   call (MR::getRandom(0, 1407)) a make during play makes one (1), and the dead one appears through
   LiveActor::appear.
   Names fold case as the game's factory does (f0e3e562a): make "kuribo" makes a Kuribo; in start a class LcMkCase is
   defined, "lcmkcase" is refused as taken (-2) and class_of("LCMKCASE") finds it; LcMkSign replaces "signboard" and
   LcMkSign2 replacing "SIGNBOARD" is refused (-2, replaced already). */
#include "../loader/lt.h"

#define KURIBOS 8

static int32_t f_pos, f_scale, f_flag, f_dead, f_nograv;
static int32_t c_kuribo;
static void* made[KURIBOS];
static void* dead_one;

static float* vec(void* object, int32_t offset) {
    return (float*)((char*)object + offset);
}

static int near(float a, float b) {
    const float d = a - b;
    return d < 0.01f && d > -0.01f;
}

static int is_dead(void* object) {
    return *((const unsigned char*)object + f_flag + f_dead) != 0;
}

static int32_t on_mario(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    /* Every scene that makes its objects (the title's, then the stage's): the last scene's objects are the ones the
       harness's call at frame 150 finds alive; the title's are freed with its scene. */
    LumaMakePlace places[KURIBOS];
    for (int i = 0; i < KURIBOS; i++) {
        LumaMakePlace* const p = &places[i];
        p->size = sizeof(LumaMakePlace);
        p->pos[0] = 1000.0f + 200.0f * (float)i;
        p->pos[1] = 500.0f;
        p->pos[2] = -300.0f - 10.0f * (float)i;
        p->rot[0] = p->rot[1] = p->rot[2] = 0.0f;
        p->scale[0] = p->scale[1] = p->scale[2] = 1.0f + 0.25f * (float)(i % 3);
        for (int k = 0; k < 8; k++) {
            p->args[k] = -1;
        }
        p->args[0] = i < 4 ? 1 : -1;
    }
    lt_fact("made8", lt_core->make(lt_mod, "Kuribo", KURIBOS, places, 0, made));
    int pos_ok = 1, scale_ok = 1, arg_ok = 1, class_ok = 1, alive = 1;
    for (int i = 0; i < KURIBOS; i++) {
        if (made[i] == 0) {
            pos_ok = scale_ok = arg_ok = class_ok = alive = 0;
            continue;
        }
        const float* const pos = vec(made[i], f_pos);
        const float* const scale = vec(made[i], f_scale);
        for (int k = 0; k < 3; k++) {
            pos_ok &= near(pos[k], places[i].pos[k]);
            scale_ok &= near(scale[k], places[i].scale[k]);
        }
        arg_ok &= *((const unsigned char*)made[i] + f_nograv) == (i < 4 ? 1 : 0);
        class_ok &= lt_core->own_class(lt_mod, made[i]) == c_kuribo;
        alive &= !is_dead(made[i]);
    }
    lt_fact("pos_ok", pos_ok);
    lt_fact("scale_ok", scale_ok);
    lt_fact("arg_ok", arg_ok);
    lt_fact("class_ok", class_ok);
    lt_fact("alive_ok", alive);
    lt_fact("made_dead", lt_core->make(lt_mod, "Kuribo", 1, 0, LUMA_MAKE_DEAD, &dead_one));
    lt_fact("dead_ok", dead_one != 0 && is_dead(dead_one));
    void* none = 0;
    lt_fact("unknown", lt_core->make(lt_mod, "LcNoSuchObject", 1, 0, 0, &none));
    lt_fact("unknown_none", none == 0);
    lt_fact("bad_count", lt_core->make(lt_mod, "Kuribo", 0, 0, 0, 0));
    lt_fact("bad_flag", lt_core->make(lt_mod, "Kuribo", 1, 0, 0x80u, 0));
    void* lower = 0;
    lt_fact("lower_made", lt_core->make(lt_mod, "kuribo", 1, &places[0], 0, &lower));
    lt_fact("lower_class", lower != 0 && lt_core->own_class(lt_mod, lower) == c_kuribo);
    return 0;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 1407) {
        return 0;
    }
    lt_fact("outside_frame", lt_core->make(lt_mod, "Kuribo", 1, 0, 0, 0));
    if (dead_one != 0) {
        lt_fact("appear_call", lt_core->call_method(lt_mod, dead_one, "LiveActor::appear", "void()", 0, 0));
        lt_fact("appeared", !is_dead(dead_one));
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("StageDataHolder::initPlacementMario", "void()", LUMA_AFTER, on_mario),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_FIELD(f_pos, "LiveActor.mPosition", "Vec3f"),
    LUMA_FIELD(f_scale, "LiveActor.mScale", "Vec3f"),
    LUMA_FIELD(f_flag, "LiveActor.mFlag", "LiveActorFlag"),
    LUMA_FIELD(f_dead, "LiveActorFlag.mIsDead", "bool"),
    LUMA_FIELD(f_nograv, "Kuribo.mIsNoGravitySupport", "bool"),
    LUMA_CLASS(c_kuribo, "Kuribo"),
};

static int32_t define(const LumaCore* core, const LumaMod* mod, const char* name, const char* replaces) {
    LumaClassDef d = {.size = sizeof(LumaClassDef), .name = name, .base = "JetTurtle", .replaces = replaces};
    return core->define_class(mod, &d);
}

LT_START({
    lt_fact("outside_start", core->make(mod, "Kuribo", 1, 0, 0, 0));
    const int32_t one = define(core, mod, "LcMkCase", 0);
    lt_fact("case_one", one >= 0);
    lt_fact("case_taken", define(core, mod, "lcmkcase", 0));
    lt_fact("case_named", one >= 0 && core->class_of("LCMKCASE") == one);
    lt_fact("case_replaces", define(core, mod, "LcMkSign", "signboard") >= 0);
    lt_fact("case_replaced_twice", define(core, mod, "LcMkSign2", "SIGNBOARD"));
})
LUMA_MOD(imports, start, 0);
