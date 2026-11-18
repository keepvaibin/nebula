/* 2964-player-reuse (M-030; contract services-player). pl-reuse: run step MR::getRandom(0, 4292 + k): k 0 makes three
   extra players; each k from 1 to 40 removes the oldest of them and makes one in its place. "made": the makes that gave
   LUMA_OK (43); "inits": the MarioActor::init2 calls during them (3: a removed player stands again in place of a new
   one, so a scene holds no more extra players than LUMA_PLAYER_MOST_EXTRA however often a mod makes and removes);
   "last_dead": MR::isDead on the last made (0: it stands). They stand beside the start: picture f120 shows them back. */
#include "../loader/lt.h"
#include <luma_player.h>

static const LumaPlayerService* player;
static const LumaFunction* f_dead;
static int32_t making, inits, made;
static LumaActor ring[3];
static int32_t oldest;

static int32_t on_init(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (making) inits++;
    return 0;
}

static void make_at(int32_t i, float x) {
    LumaPlayerInit init = {0};
    init.size = (uint32_t)sizeof(LumaPlayerInit);
    init.input = LUMA_PLAYER_INPUT_NONE;
    init.pos[0] = x;
    init.pos[1] = -12981.3f; /* beside scenario 1's start, in view */
    init.pos[2] = -15332.0f;
    making = 1;
    made += player->make(lt_mod, &init, &ring[i]) == LUMA_OK;
    making = 0;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4292;
    if (LUMA_ARG(call, 1, int32_t) != 0 || k < 0 || k > 40) return 0;
    if (k == 0) {
        for (int32_t i = 0; i < 3; i++) make_at(i, -2864.6f - 150.0f * (float)i);
    } else {
        lt_fact("remove", player->remove(lt_mod, ring[oldest]));
        make_at(oldest, -2864.6f - 150.0f * (float)(k % 5));
        if (k == 40) {
            _Bool dead = 1;
            void* args[2] = {0, (void*)&ring[oldest]};
            f_dead->invoke(args, &dead);
            lt_fact("last_dead", dead);
        }
        oldest = (oldest + 1) % 3;
    }
    lt_fact("made", made);
    lt_fact("inits", inits);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(player, LUMA_PLAYER_NAME, "1.0"),
    LUMA_FUNCTION(f_dead, "MR::isDead", "bool(const LiveActor *)"),
    LUMA_HOOK_ON("MarioActor::init2", "void(const Vec3f &, const Vec3f &, s32)", LUMA_BEFORE, on_init),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
