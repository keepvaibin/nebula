/* 2963-hidden-carry (M-064). pl-carry: class PcShell from JetTurtle (the shell the player carries) in place of every
   Kuribo the stage places. Run steps call MR::getRandom(0, 4300 + k); the hook gives 1.
   k 1: the game player's body hidden (set_drawn SHADOW: "hide"), then he takes the first shell with his own
        MarioActor::tryGetItem ("taken").
   k 2: the shell is hidden while he holds it ("held_hidden"); he throws it with MarioActor::releaseThrowMemoSensor
        ("thrown").
   k 3: the thrown shell is drawn again though his body is still hidden ("thrown_shown"), the body still hidden
        ("body_hidden"); then he is drawn whole (set_drawn ALL: "whole").
   k 4: the shell is drawn, unless it broke on a hit and the game hid it itself (JetTurtle::isRestart): "end_shown". */
#include "../loader/lt.h"
#include <luma_player.h>

static const LumaPlayerService* player;
static const LumaFunction* f_actor;   /* MarioActor *MarioAccess::getPlayerActor() */
static const LumaFunction* f_sensor;  /* LiveActor::getSensor */
static const LumaFunction* f_take;    /* MarioActor::tryGetItem */
static const LumaFunction* f_release; /* MarioActor::releaseThrowMemoSensor */
static const LumaFunction* f_hidden;  /* bool MR::isHiddenModel(const LiveActor *) */
static const LumaFunction* f_restart; /* bool JetTurtle::isRestart() const */
static int32_t c_live;
static int32_t c_jet;
static int32_t mod_hidden_at;          /* MarioActor.mModHidden */
static void* volatile first;

static void made(void* object, void* data, void* user) {
    (void)data;
    (void)user;
    if (first == 0) first = object;
}

static const LumaClassDef shell_def = {
    .size = sizeof(LumaClassDef), .name = "PcShell", .base = "JetTurtle", .replaces = "Kuribo", .data_size = 4,
    .made = made,
};

static void* mario(void) {
    void* none[] = {0};
    void* m = 0;
    f_actor->invoke(none, &m);
    return m;
}

static int hidden(void* actor) {
    void* live = (void*)lt_core->as_class(lt_mod, actor, c_live);
    void* args[] = {0, &live};
    _Bool h = 0;
    f_hidden->invoke(args, &h);
    return h != 0;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4300;
    if (k < 1 || k > 4) return 0;
    void* const m = mario();
    void* const shell = first;
    if (m == 0 || shell == 0) {
        lt_fact("missing", (m == 0 ? 1 : 0) + (shell == 0 ? 2 : 0));
        LUMA_RESULT(call, int32_t) = 1;
        return 0;
    }
    if (k == 1) {
        lt_fact("hide", player->set_drawn(lt_mod, (LumaActor)m, LUMA_PLAYER_DRAWN_SHADOW));
        const char* body = "body";
        void* args[] = {(void*)lt_core->as_class(lt_mod, shell, c_live), &body};
        void* sensor = 0;
        f_sensor->invoke(args, &sensor);
        void* take[] = {m, &sensor};
        _Bool took = 0;
        if (sensor != 0) f_take->invoke(take, &took);
        lt_fact("taken", took != 0);
    } else if (k == 2) {
        lt_fact("held_hidden", hidden(shell));
        void* args[] = {m};
        _Bool threw = 0;
        f_release->invoke(args, &threw);
        lt_fact("thrown", threw != 0);
    } else if (k == 3) {
        lt_fact("thrown_shown", !hidden(shell));
        lt_fact("body_hidden", (((const unsigned char*)m)[mod_hidden_at] & 1u) != 0);
        lt_fact("whole", player->set_drawn(lt_mod, (LumaActor)m, LUMA_PLAYER_DRAWN_ALL));
    } else {
        void* args[] = {(void*)lt_core->as_class(lt_mod, shell, c_jet)};
        _Bool broke = 0;
        f_restart->invoke(args, &broke);
        lt_fact("end_shown", !hidden(shell) || broke != 0);
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(player, LUMA_PLAYER_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_FUNCTION(f_actor, "MarioAccess::getPlayerActor", "MarioActor *()"),
    LUMA_FUNCTION(f_sensor, "LiveActor::getSensor", "HitSensor *(const char *) const"),
    LUMA_FUNCTION(f_take, "MarioActor::tryGetItem", "bool(HitSensor *)"),
    LUMA_FUNCTION(f_release, "MarioActor::releaseThrowMemoSensor", "bool()"),
    LUMA_FUNCTION(f_hidden, "MR::isHiddenModel", "bool(const LiveActor *)"),
    LUMA_FIELD(mod_hidden_at, "MarioActor.mModHidden", "u8"),
    LUMA_FUNCTION(f_restart, "JetTurtle::isRestart", "bool() const"),
    LUMA_CLASS(c_live, "LiveActor"),
    LUMA_CLASS(c_jet, "JetTurtle"),
};

LT_START({ lt_fact("defined", core->define_class(mod, &shell_def) >= 0); })
LUMA_MOD(imports, start, 0);
