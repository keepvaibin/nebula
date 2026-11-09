/* No fall-out death (Gecko census, MOD-PLAN M9): Falling out of a level puts the player back where they last stood. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static float ground[3];
static int have_ground;

static int32_t stage_start(const NebulaModEventData* data, void* user) {
    (void)user;
    have_ground = 0;
    return data->value;
}

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    NebulaActor* player = game->player();
    (void)user;
    if (player != 0 && game->value(NEBULA_VALUE_ON_GROUND)) {
        game->get_position(player, ground);
        have_ground = 1;
    }
    return data->value;
}

/* How 0: a fall out of the level. -1 refuses it. */
static int32_t miss(const NebulaModEventData* data, void* user) {
    const float still[3] = {0.0f, 0.0f, 0.0f};
    NebulaActor* player = game->player();
    (void)user;
    if (data->value != 0 || !have_ground || player == 0) {
        return data->value;
    }
    game->set_velocity(player, still);
    game->set_position(player, ground);
    return -1;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_START, stage_start, 0);
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
    game->subscribe(NEBULA_MOD_EVENT_PLAYER_MISS, miss, 0);
}
