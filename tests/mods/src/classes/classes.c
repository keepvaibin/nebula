/* F's 1.4 conformance mods (contract classes; plan 1.4: a mod class from any game class, made by the game's own
   factory, its overrides hooks filtered to its instances, with its own data; placeable by name in a stage).
   lc-shell  (-DCASE=1): class LcShell from JetTurtle (the shell the player carries), made in place of every Kuribo the
                         stage places, 16 bytes of its own, three overrides: JetTurtle::receiveOtherMsg (counts a take the
                         base answered yes to), JetTurtle::receiveMsgThrow (counts a throw), LiveActor::calcAnim (a row
                         every actor shares: counts its own objects, and any other object it ran for, which must be none).
                         An after hook of its own on LiveActor::calcAnim counts the other actors' calls, which run the
                         game's function with no override. MR::getRandom(0, 1401): the class's answers, then the player
                         takes the first object with its own MarioActor::tryGetItem; (0, 1402): the player throws it with
                         its own MarioActor::releaseThrowMemoSensor (tryThrow less the button test).
   lc-refuse (-DCASE=2): each refusal of define_class, by its number (facts r_*), and two classes that are defined.
   lc-watch  (-DCASE=3): with lc-shell, for its take-out: (0, 1403) before and (0, 1404) after: class_of("LcShell"), and
                         own_class of a JetTurtle it saw (LcShell's number before, JetTurtle's after).
   lc-gone   (-DCASE=4): class LcGone from JetTurtle in place of every Kuribo, with `gone` (contract classes, "gone"):
                         each object's data holds a mark and its serial; `made` counts it, `gone` checks the data is that
                         object's (the mark, a serial made and not gone before) and counts it.
   lc-gone-old (-DCASE=5): the same class with `gone` set but the definition's size ending before it (a mod built for a
                         LumaClassDef without it): defined, made, and its `gone` never called.
   lc-slots  (-DCASE=6): class LcSlots from Kuribo in place of every Kuribo, overrides by slot of LiveActor's inline
                         virtuals (f/1.3-slots): control, receiveMsgPlayerAttack, receiveMsgTake, seen to run.
   Cases 4 and 5 need a luma.h with LumaClassDef.gone (build.sh builds them only then). */
#include "../loader/lt.h"

#ifndef CASE
#error CASE
#endif

#if CASE == 1
typedef struct ShellData {
    int32_t index; /* 1 for the first object made */
    int32_t taken;
    int32_t thrown;
    int32_t calcs;
} ShellData;

static int32_t shell_class = -1;
static volatile int32_t made_count;
static void* volatile first;
static volatile int32_t calc_own, calc_wrong, calc_others;
static const LumaFunction* f_player;   /* MR::getPlayerDemoActor */
static const LumaFunction* f_sensor;   /* LiveActor::getSensor */
static const LumaFunction* f_take;     /* MarioActor::tryGetItem */
static const LumaFunction* f_release;  /* MarioActor::releaseThrowMemoSensor */
static const LumaFunction* f_is_get;   /* MR::isMsgItemGet */
static int32_t c_base, c_mario, c_live;

static ShellData* data_of(const void* object) {
    return (ShellData*)lt_core->class_data(lt_mod, object, shell_class);
}

static void made(void* object, void* data, void* user) {
    (void)user;
    ShellData* const d = (ShellData*)data;
    const int32_t zero = d->index == 0 && d->taken == 0 && d->thrown == 0 && d->calcs == 0;
    d->index = __atomic_add_fetch(&made_count, 1, __ATOMIC_RELAXED);
    if (d->index == 1) {
        first = object;
    }
    lt_fact("made", d->index);
    lt_fact("made_zeroed", zero);
    lt_fact("made_data_found", data_of(object) == d);
}

static int32_t on_other_msg(LumaCall* call, void* user) {
    (void)user;
    lt_core->proceed(call);
    void* args[2] = {0, call->args[1]};
    _Bool is_get = 0;
    f_is_get->invoke(args, &is_get);
    ShellData* const d = data_of(call->args[0]);
    if (is_get && d != 0 && LUMA_RESULT(call, _Bool)) {
        d->taken++;
        lt_fact("taken", d->taken);
        lt_fact("taken_index", d->index);
    }
    return 0;
}

static int32_t on_throw(LumaCall* call, void* user) {
    (void)user;
    lt_core->proceed(call);
    ShellData* const d = data_of(call->args[0]);
    if (d != 0 && LUMA_RESULT(call, _Bool)) {
        d->thrown++;
        lt_fact("thrown", d->thrown);
        lt_fact("thrown_index", d->index);
    }
    return 0;
}

static int32_t on_calc(LumaCall* call, void* user) {
    (void)user;
    ShellData* const d = data_of(call->args[0]);
    if (d != 0) {
        d->calcs++;
        __atomic_add_fetch(&calc_own, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&calc_wrong, 1, __ATOMIC_RELAXED);
    }
    lt_core->proceed(call);
    return 0;
}

static int32_t watch_calc(LumaCall* call, void* user) {
    (void)user;
    if (data_of(call->args[0]) == 0) {
        __atomic_add_fetch(&calc_others, 1, __ATOMIC_RELAXED);
    }
    return 0;
}

static void* call1(const LumaFunction* fn, void* self, void* arg) {
    void* args[2] = {self, arg};
    void* got = 0;
    fn->invoke(args, &got);
    return got;
}

static void checks(void) {
    void* const shell = first;
    void* const actor = call1(f_player, 0, 0);
    void* const mario = actor != 0 ? (void*)lt_core->as_class(lt_mod, actor, c_mario) : 0;
    lt_fact("has_first", shell != 0);
    lt_fact("has_player", mario != 0);
    lt_fact("number_same", shell_class >= 0 && lt_core->class_of("LcShell") == shell_class);
    if (shell == 0 || mario == 0) {
        return;
    }
    lt_fact("own_is_class", lt_core->own_class(lt_mod, shell) == shell_class);
    lt_fact("is_class", lt_core->is_a(shell, shell_class));
    lt_fact("is_base", lt_core->is_a(shell, c_base));
    lt_fact("as_same", lt_core->as_class(lt_mod, shell, shell_class) == shell);
    lt_fact("player_not", lt_core->is_a(mario, shell_class) == 0 && lt_core->as_class(lt_mod, mario, shell_class) == 0);
    lt_fact("player_no_data", data_of(mario) == 0);
    lt_fact("first_index", data_of(shell) != 0 ? data_of(shell)->index : -1);
    lt_fact("calc_own", calc_own);
    lt_fact("calc_wrong", calc_wrong);
    lt_fact("calc_others", calc_others);
    lt_fact("first_calcs", data_of(shell) != 0 ? data_of(shell)->calcs : -1);
    const char* body = "body";
    void* const sensor = call1(f_sensor, (void*)lt_core->as_class(lt_mod, shell, c_live), &body);
    lt_fact("has_sensor", sensor != 0);
    if (sensor == 0) {
        return;
    }
    void* held = sensor;
    void* args[2] = {mario, &held};
    _Bool took = 0;
    f_take->invoke(args, &took);
    lt_fact("take_answer", took);
}

static void throw_it(void) {
    void* const actor = call1(f_player, 0, 0);
    void* const mario = actor != 0 ? (void*)lt_core->as_class(lt_mod, actor, c_mario) : 0;
    if (mario == 0) {
        lt_fact("throw_answer", -1);
        return;
    }
    void* args[1] = {mario};
    _Bool threw = 0;
    f_release->invoke(args, &threw);
    lt_fact("throw_answer", threw);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t which = LUMA_ARG(call, 2, int32_t);
    if (which == 1401) {
        checks();
    } else if (which == 1402) {
        throw_it();
    }
    return 0;
}

static const LumaOverride overrides[] = {
    {"JetTurtle::receiveOtherMsg", "bool(u32, HitSensor *, HitSensor *)", on_other_msg, 0},
    {"JetTurtle::receiveMsgThrow", "bool(HitSensor *, HitSensor *)", on_throw, 0},
    {"LiveActor::calcAnim", "void()", on_calc, 0},
};

static const LumaClassDef shell_def = {
    .size = sizeof(LumaClassDef), .name = "LcShell", .base = "JetTurtle", .replaces = "Kuribo",
    .data_size = sizeof(ShellData), .made = made, .overrides = overrides,
    .override_count = sizeof(overrides) / sizeof(overrides[0]),
};

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_HOOK_ON("LiveActor::calcAnim", "void()", LUMA_AFTER, watch_calc),
    LUMA_FUNCTION(f_player, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_FUNCTION(f_sensor, "LiveActor::getSensor", "HitSensor *(const char *) const"),
    LUMA_FUNCTION(f_take, "MarioActor::tryGetItem", "bool(HitSensor *)"),
    LUMA_FUNCTION(f_release, "MarioActor::releaseThrowMemoSensor", "bool()"),
    LUMA_FUNCTION(f_is_get, "MR::isMsgItemGet", "bool(u32)"),
    LUMA_CLASS(c_base, "JetTurtle"),
    LUMA_CLASS(c_mario, "MarioActor"),
    LUMA_CLASS(c_live, "LiveActor"),
};

LT_START({
    shell_class = core->define_class(mod, &shell_def);
    lt_fact("defined", shell_class >= 0);
})
LUMA_MOD(imports, start, 0);

#elif CASE == 2
static void made(void* object, void* data, void* user) {
    (void)object;
    (void)data;
    (void)user;
}

static int32_t pass(LumaCall* call, void* user) {
    (void)user;
    lt_core->proceed(call);
    return 0;
}

static int32_t define(const char* name, const char* base, const char* maker, const char* replaces, const LumaOverride* o,
                      uint32_t count, uint32_t size, uint32_t align) {
    LumaClassDef d = {.size = sizeof(LumaClassDef), .name = name, .base = base, .maker = maker, .replaces = replaces,
                      .data_size = 8, .data_align = align, .made = made, .overrides = o, .override_count = count};
    d.size = size;
    return lt_core->define_class(lt_mod, &d);
}

static const LumaOverride no_virtual[] = {{"JetTurtle::noSuchFunction", "void()", pass, 0}};
static const LumaOverride not_base[] = {{"Kuribo::control", "void()", pass, 0}};
static const LumaOverride no_hook[] = {{"JetTurtle::control", "void()", 0, 0}};

LT_START({
    const uint32_t s = sizeof(LumaClassDef);
    lt_fact("r_null", core->define_class(mod, 0));
    lt_fact("r_small", define("LcR0", "JetTurtle", 0, 0, 0, 0, 4, 0));
    lt_fact("r_no_name", define("", "JetTurtle", 0, 0, 0, 0, s, 0));
    lt_fact("r_long_name", define("LcRAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "JetTurtle", 0, 0, 0, 0, s, 0));
    lt_fact("r_no_base", define("LcR0", "NoSuchClass", 0, 0, 0, 0, s, 0));
    lt_fact("r_align", define("LcR0", "JetTurtle", 0, 0, 0, 0, s, 24));
    lt_fact("r_name_game_class", define("Kuribo", "JetTurtle", 0, 0, 0, 0, s, 0));
    lt_fact("r_name_factory", define("Bomb", "JetTurtle", 0, 0, 0, 0, s, 0));
    lt_fact("r_no_virtual", define("LcR0", "JetTurtle", 0, 0, no_virtual, 1, s, 0));
    lt_fact("r_not_base", define("LcR0", "JetTurtle", 0, 0, not_base, 1, s, 0));
    lt_fact("r_no_hook", define("LcR0", "JetTurtle", 0, 0, no_hook, 1, s, 0));
    lt_fact("r_no_maker", define("LcR0", "JetTurtle", "NoSuchMaker", 0, 0, 0, s, 0));
    lt_fact("r_no_placement", define("LcR0", "JetTurtle", 0, "NoSuchPlacement", 0, 0, s, 0));
    const int32_t one = define("LcR1", "JetTurtle", 0, 0, 0, 0, s, 0);
    lt_fact("r_one", one >= 0);
    lt_fact("r_one_named", one >= 0 && core->class_of("LcR1") == one);
    lt_fact("r_again", define("LcR1", "JetTurtle", 0, 0, 0, 0, s, 0));
    const int32_t two = define("LcR2", "JetTurtle", 0, "SignBoard", 0, 0, s, 0);
    lt_fact("r_two", two >= 0 && two != one);
    lt_fact("r_replaced_twice", define("LcR3", "JetTurtle", 0, "SignBoard", 0, 0, s, 0));
    lt_fact("r_name_is_replaced", define("SignBoard", "JetTurtle", 0, 0, 0, 0, s, 0));
    lt_fact("r_unknown_number", core->class_of("LcR0"));
})
LUMA_MOD_NO_IMPORTS(start, 0);

#elif CASE == 3
static int32_t c_base;
static const void* volatile seen; /* a JetTurtle (an LcShell while lc-shell is on) */

static int32_t look(LumaCall* call, void* user) {
    (void)user;
    if (seen == 0 && lt_core->is_a(call->args[0], c_base)) {
        seen = call->args[0];
    }
    return 0;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t which = LUMA_ARG(call, 2, int32_t);
    if (which != 1403 && which != 1404) {
        return 0;
    }
    const char* const when = which == 1403 ? "before" : "after";
    const int32_t number = lt_core->class_of("LcShell");
    const int32_t own = seen != 0 ? lt_core->own_class(lt_mod, seen) : -9;
    char key[48];
    char* at = lt_put(key, "number_");
    at = lt_put(at, when);
    *at = 0;
    lt_fact(key, number >= 0 ? 1 : number);
    at = lt_put(key, "own_is_shell_");
    at = lt_put(at, when);
    *at = 0;
    lt_fact(key, number >= 0 && own == number);
    at = lt_put(key, "own_is_base_");
    at = lt_put(at, when);
    *at = 0;
    lt_fact(key, own == c_base);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_HOOK_ON("LiveActor::calcAnim", "void()", LUMA_AFTER, look),
    LUMA_CLASS(c_base, "JetTurtle"),
};

LT_START({})
LUMA_MOD(imports, start, 0);

#elif CASE == 4 || CASE == 5
#define GONE_MARK 0x60E60E60
#define GONE_MOST 1024
typedef struct GoneData {
    int32_t mark;
    int32_t serial; /* 1 for the first object made */
} GoneData;

static volatile int32_t made_count, gone_count;
static unsigned char gone_seen[GONE_MOST + 1];

static void made(void* object, void* data, void* user) {
    (void)object;
    (void)user;
    GoneData* const d = (GoneData*)data;
    d->mark = GONE_MARK;
    d->serial = ++made_count;
    lt_fact("made", d->serial);
}

static void gone(void* object, void* data, void* user) {
    (void)object;
    (void)user;
    const GoneData* const d = (const GoneData*)data;
    const int ok = d != 0 && d->mark == GONE_MARK && d->serial >= 1 && d->serial <= made_count && d->serial <= GONE_MOST;
    lt_fact("gone_data_ok", ok);
    if (ok) {
        lt_fact("gone_once", gone_seen[d->serial] == 0);
        gone_seen[d->serial] = 1;
    }
    lt_fact("gone", ++gone_count);
}

static LumaClassDef gone_def = {
    .size = sizeof(LumaClassDef), .name = "LcGone", .base = "JetTurtle", .replaces = "Kuribo",
    .data_size = sizeof(GoneData), .made = made, .gone = gone,
};

LT_START({
#if CASE == 5
    gone_def.size = (uint32_t)__builtin_offsetof(LumaClassDef, gone);
#endif
    lt_fact("defined", core->define_class(mod, &gone_def) >= 0);
})
LUMA_MOD_NO_IMPORTS(start, 0);
#elif CASE == 6
/* lc-slots: class LcSlots from Kuribo in place of every Kuribo, three overrides by slot named by LiveActor's inline
   declarations (no function row of their own): control (Kuribo's own code runs on proceed), receiveMsgPlayerAttack
   (Kuribo's), receiveMsgTake (LiveActor's inline body: Kuribo has none). MR::getRandom(0, 1408): virtual calls on the
   first object through LUMA_METHOD: an attack with the mark 0x1408 is answered by the hook (no proceed), a take
   proceeds to the inline body (false). */
typedef struct SlotData {
    int32_t index;
    int32_t controls;
} SlotData;

static int32_t slot_class = -1;
static volatile int32_t made_count;
static void* volatile first;
/* Every made object (at most 64): the busiest one's own data proves the slot's count is per object (the first one may be
   clipped all run and never controlled). */
static void* volatile made_objects[64];
static volatile int32_t control_own, control_wrong, attack_marked, attack_other, take_own, take_wrong;
static const LumaFunction* m_attack; /* LiveActor::receiveMsgPlayerAttack, the virtual call */
static const LumaFunction* m_take;   /* LiveActor::receiveMsgTake, the virtual call */
static int32_t c_base;

static SlotData* data_of(const void* object) {
    return (SlotData*)lt_core->class_data(lt_mod, object, slot_class);
}

static void made(void* object, void* data, void* user) {
    (void)user;
    SlotData* const d = (SlotData*)data;
    d->index = __atomic_add_fetch(&made_count, 1, __ATOMIC_RELAXED);
    if (d->index == 1) {
        first = object;
    }
    if (d->index >= 1 && d->index <= 64) {
        made_objects[d->index - 1] = object;
    }
    lt_fact("made", d->index);
    lt_fact("made_own_class", lt_core->own_class(lt_mod, object) == slot_class);
    lt_fact("made_is_base", lt_core->is_a(object, c_base));
}

static int32_t on_control(LumaCall* call, void* user) {
    (void)user;
    SlotData* const d = data_of(call->args[0]);
    if (d != 0) {
        d->controls++;
        __atomic_add_fetch(&control_own, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&control_wrong, 1, __ATOMIC_RELAXED);
    }
    lt_core->proceed(call);
    return 0;
}

static int32_t on_attack(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, uint32_t) == 0x1408u && data_of(call->args[0]) != 0) {
        __atomic_add_fetch(&attack_marked, 1, __ATOMIC_RELAXED);
        LUMA_RESULT(call, _Bool) = 1;
        return 0;
    }
    __atomic_add_fetch(&attack_other, 1, __ATOMIC_RELAXED);
    lt_core->proceed(call);
    return 0;
}

static int32_t on_take(LumaCall* call, void* user) {
    (void)user;
    if (data_of(call->args[0]) != 0) {
        __atomic_add_fetch(&take_own, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&take_wrong, 1, __ATOMIC_RELAXED);
    }
    lt_core->proceed(call);
    lt_fact("take_proceeded", LUMA_RESULT(call, _Bool));
    return 0;
}

static void checks(void) {
    void* const object = first;
    lt_fact("has_first", object != 0);
    if (object == 0) {
        return;
    }
    lt_fact("own_is_class", lt_core->own_class(lt_mod, object) == slot_class);
    int32_t most_controls = -1;
    for (int i = 0; i < 64; i++) {
        SlotData* const d = made_objects[i] != 0 ? data_of(made_objects[i]) : 0;
        if (d != 0 && d->controls > most_controls) {
            most_controls = d->controls;
        }
    }
    lt_fact("first_controls", data_of(object) != 0 ? data_of(object)->controls : -1);
    lt_fact("most_controls", most_controls);
    lt_fact("control_own", control_own);
    lt_fact("control_wrong", control_wrong);
    uint32_t mark = 0x1408u;
    void* none = 0;
    void* attack[4] = {object, &mark, &none, &none};
    _Bool answered = 0;
    m_attack->invoke(attack, &answered);
    lt_fact("attack_answer", answered);
    lt_fact("attack_marked", attack_marked);
    void* take[3] = {object, &none, &none};
    _Bool took = 1;
    m_take->invoke(take, &took);
    lt_fact("take_answer", took);
    lt_fact("take_own", take_own);
    lt_fact("take_wrong", take_wrong);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 1408) {
        checks();
    }
    return 0;
}

static const LumaOverride overrides[] = {
    {"LiveActor::control", "void()", on_control, 0},
    {"LiveActor::receiveMsgPlayerAttack", "bool(u32, HitSensor *, HitSensor *)", on_attack, 0},
    {"LiveActor::receiveMsgTake", "bool(HitSensor *, HitSensor *)", on_take, 0},
};

static const LumaClassDef slots_def = {
    .size = sizeof(LumaClassDef), .name = "LcSlots", .base = "Kuribo", .replaces = "Kuribo",
    .data_size = sizeof(SlotData), .made = made, .overrides = overrides,
    .override_count = sizeof(overrides) / sizeof(overrides[0]),
};

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_METHOD(m_attack, "LiveActor::receiveMsgPlayerAttack", "bool(u32, HitSensor *, HitSensor *)"),
    LUMA_METHOD(m_take, "LiveActor::receiveMsgTake", "bool(HitSensor *, HitSensor *)"),
    LUMA_CLASS(c_base, "Kuribo"),
};

LT_START({
    slot_class = core->define_class(mod, &slots_def);
    lt_fact("defined", slot_class >= 0);
})
LUMA_MOD(imports, start, 0);
#endif
