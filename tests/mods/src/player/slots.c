/* 2965-player-drawn-slots (M-065; contract services-player). pl-slots (CASE 1) at run step MR::getRandom(0, 4340):
   three extra players made and each drawn as its shadow only, then removed; one more made and drawn so; then the
   game's player drawn as his shadow only ("hide_player": LUMA_OK). The test takes pl-slots out. pl-look (CASE 2) at
   MR::getRandom(0, 4341), after: "player_hidden", MarioActor::isAllHidden on the game's player (0: the mod's end gave
   him back whole, as set_drawn says; 1: the hide outlived its mod). */
#include "../loader/lt.h"
#include <luma_player.h>

static const LumaFunction* f_player;

static void* game_player(void) {
    void* actor = 0;
    void* args[1] = {0};
    f_player->invoke(args, &actor);
    return actor;
}

#if CASE == 1
static const LumaPlayerService* player;

static int32_t make_shadow(float x, LumaActor* out) {
    LumaPlayerInit init = {0};
    init.size = (uint32_t)sizeof(LumaPlayerInit);
    init.input = LUMA_PLAYER_INPUT_NONE;
    init.pos[0] = x;
    const int32_t status = player->make(lt_mod, &init, out);
    return status != LUMA_OK ? status : player->set_drawn(lt_mod, *out, LUMA_PLAYER_DRAWN_SHADOW);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, int32_t) != 0 || LUMA_ARG(call, 2, int32_t) != 4340) return 0;
    LumaActor made[3] = {0, 0, 0};
    int32_t ok = 0;
    for (int32_t i = 0; i < 3; i++) ok += make_shadow(100.0f * (float)(i + 1), &made[i]) == LUMA_OK;
    for (int32_t i = 0; i < 3; i++) ok += player->remove(lt_mod, made[i]) == LUMA_OK;
    LumaActor last = 0;
    ok += make_shadow(400.0f, &last) == LUMA_OK;
    lt_fact("steps_ok", ok);
    lt_fact("hide_player", player->set_drawn(lt_mod, game_player(), LUMA_PLAYER_DRAWN_SHADOW));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(player, LUMA_PLAYER_NAME, "1.1"),
    LUMA_FUNCTION(f_player, "MarioAccess::getPlayerActor", "MarioActor *()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};
#else
static const LumaFunction* f_hidden;

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, int32_t) != 0 || LUMA_ARG(call, 2, int32_t) != 4341) return 0;
    _Bool hidden = 1;
    void* args[1] = {game_player()};
    f_hidden->invoke(args, &hidden);
    lt_fact("player_hidden", hidden);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(f_player, "MarioAccess::getPlayerActor", "MarioActor *()"),
    LUMA_FUNCTION(f_hidden, "MarioActor::isAllHidden", "bool() const"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};
#endif

LT_START({})
LUMA_MOD(imports, start, 0);
