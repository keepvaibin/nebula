/* 2962-player-draw-time (contract services-player). pl-draw: run step MR::getRandom(0, 4291) arms it; the next
   GameScene::draw (before it: this frame's calc is done, nothing of it drawn yet) makes one extra player ("drawn_make":
   LUMA_OK) and makes and removes another ("drawn_remove": LUMA_OK). Neither has a pose until its first calc: the
   game must draw neither this frame. The first stands and is drawn in the frames after. */
#include "../loader/lt.h"
#include <luma_player.h>

static const LumaPlayerService* player;
static int armed;

static LumaPlayerInit init_at(float x) {
    LumaPlayerInit init = {0};
    init.size = (uint32_t)sizeof(LumaPlayerInit);
    init.input = LUMA_PLAYER_INPUT_NONE;
    init.pad = 0;
    init.pos[0] = x;
    init.pos[1] = 0.0f;
    init.pos[2] = 0.0f;
    return init;
}

static int32_t arm(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4291) return 0;
    armed = 1;
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static int32_t on_draw(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (!armed) return 0;
    armed = 0;
    LumaPlayerInit first = init_at(150.0f);
    LumaActor stays = 0;
    lt_fact("drawn_make", player->make(lt_mod, &first, &stays));
    LumaPlayerInit second = init_at(-150.0f);
    LumaActor goes = 0;
    int32_t made = player->make(lt_mod, &second, &goes);
    lt_fact("drawn_remove", made != LUMA_OK ? made : player->remove(lt_mod, goes));
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(player, LUMA_PLAYER_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, arm),
    LUMA_HOOK_ON("GameScene::draw", "void() const", LUMA_BEFORE, on_draw),
};

LT_START({})
LUMA_MOD(imports, start, 0);
