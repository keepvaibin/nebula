/* lt-released: objects that go (loader contract 7). A before on lumacore::released(const void *, const void *) drops a
   kept handle whose object lies in the told range. MR::getRandom(0, 4251) keeps the player (MR::getPlayerDemoActor);
   the player falls, the stage starts again (its arenas destroyed and told); MR::getRandom(0, 4252) reports: the handle
   is none, a range was told, and the new scene has its player. */
#include "lt.h"

static const LumaFunction* player;
static const void* kept;
static int32_t told, dropped;

static const void* the_player(void) {
    void* args[1] = {0};
    void* got = 0;
    player->invoke(args, &got);
    return got;
}

static int32_t on_released(LumaCall* call, void* user) {
    (void)user;
    const unsigned char* start = LUMA_ARG(call, 1, const unsigned char*);
    const unsigned char* end = LUMA_ARG(call, 2, const unsigned char*);
    told++;
    if (kept != 0 && (const unsigned char*)kept >= start && (const unsigned char*)kept < end) {
        kept = 0;
        dropped++;
    }
    return 0;
}

static int32_t marks(LumaCall* call, void* user) {
    (void)user;
    const int32_t m = LUMA_ARG(call, 2, int32_t);
    if (m == 4251) {
        kept = the_player();
        lt_fact("held", kept != 0);
        LUMA_RESULT(call, int32_t) = kept != 0;
    } else if (m == 4252) {
        lt_fact("none", kept == 0);
        lt_fact("dropped", dropped);
        lt_fact("told_some", told > 0);
        lt_fact("player_again", the_player() != 0); /* the new scene's (it may sit where the old one was) */
        LUMA_RESULT(call, int32_t) = kept == 0;
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(player, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_HOOK_ON("lumacore::released", "void(const void *, const void *)", LUMA_BEFORE, on_released),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, marks),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
