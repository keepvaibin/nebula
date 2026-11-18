/* 2963-player-bad-pointers (M-029). pl-badptr: run step MR::getRandom(0, 4291) on the stage: make with an init or
   out that cannot be read or written (lt_bad.h) answers LUMA_E_ARGUMENT and makes no player: three good makes after
   are LUMA_OK ("made") and the fourth LUMA_E_LIMIT ("fourth"). */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_player.h>

static const LumaPlayerService* player;

static LumaPlayerInit init_at(float x) {
    LumaPlayerInit init = {0};
    init.size = (uint32_t)sizeof(LumaPlayerInit);
    init.input = LUMA_PLAYER_INPUT_NONE;
    init.pos[0] = x;
    return init;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4291) return 0;
    LumaPlayerInit init = init_at(100.0f);
    LumaActor made[4] = {0, 0, 0, 0};
    lt_fact("init_unmapped", player->make(lt_mod, (const LumaPlayerInit*)LT_UNMAPPED, &made[0]));
    lt_fact("out_unmapped", player->make(lt_mod, &init, (LumaActor*)LT_UNMAPPED));
    lt_fact("out_read_only", player->make(lt_mod, &init, (LumaActor*)LT_READ_ONLY));
    lt_fact("none_written", made[0] == 0);
    int32_t ok = 0;
    for (int i = 0; i < 3; i++) {
        LumaPlayerInit good = init_at(100.0f * (float)(i + 1));
        ok += player->make(lt_mod, &good, &made[i]) == LUMA_OK;
    }
    lt_fact("made", ok);
    LumaPlayerInit fourth = init_at(400.0f);
    lt_fact("fourth", player->make(lt_mod, &fourth, &made[3]));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(player, LUMA_PLAYER_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
