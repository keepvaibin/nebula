/* 2961-player-extra (contract services-player). pl-extra: at start (no scene yet) make answers LUMA_E_STATE
   ("start_make"); run step MR::getRandom(0, 4290) on the stage: three extra players standing with no input ("made":
   how many LUMA_OK), the fourth LUMA_E_LIMIT ("fourth"), a pad other than 0 for NONE LUMA_E_ARGUMENT ("bad_pad"), an
   init smaller than 1.0's LUMA_E_ARGUMENT ("small"); remove of the first LUMA_OK ("remove"), again LUMA_E_HANDLE
   ("remove_again"); a make after the remove LUMA_OK ("again": a place is free). */
#include "../loader/lt.h"
#include <luma_player.h>

static const LumaPlayerService* player;

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

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4290) return 0;
    LumaActor made[4] = {0, 0, 0, 0};
    int32_t ok = 0;
    for (int i = 0; i < 3; i++) {
        LumaPlayerInit init = init_at(100.0f * (float)(i + 1));
        ok += player->make(lt_mod, &init, &made[i]) == LUMA_OK;
    }
    lt_fact("made", ok);
    LumaPlayerInit init = init_at(400.0f);
    lt_fact("fourth", player->make(lt_mod, &init, &made[3]));
    LumaPlayerInit bad = init_at(0.0f);
    bad.pad = 1;
    LumaActor none = 0;
    lt_fact("bad_pad", player->make(lt_mod, &bad, &none));
    LumaPlayerInit small = init_at(0.0f);
    small.size = 8u;
    lt_fact("small", player->make(lt_mod, &small, &none));
    lt_fact("remove", player->remove(lt_mod, made[0]));
    lt_fact("remove_again", player->remove(lt_mod, made[0]));
    LumaPlayerInit again = init_at(500.0f);
    lt_fact("again", player->make(lt_mod, &again, &made[0]));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(player, LUMA_PLAYER_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({
    LumaPlayerInit init = init_at(0.0f);
    LumaActor none = 0;
    lt_fact("start_make", player->make(lt_mod, &init, &none));
})
LUMA_MOD(imports, start, 0);
