/* Rainbow Mario forever (Gecko census, MOD-PLAN M9): The invincible star power from the start of every stage, never ending. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t stage_start(const NebulaModEventData* data, void* user) {
    (void)user;
    game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_INVINCIBLE);
    return data->value;
}

static int32_t power_up(const NebulaModEventData* data, void* user) {
    (void)user;
    return data->value == NEBULA_POWER_UP_NONE ? NEBULA_POWER_UP_INVINCIBLE : data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_START, stage_start, 0);
    game->subscribe(NEBULA_MOD_EVENT_POWER_UP, power_up, 0);
}
