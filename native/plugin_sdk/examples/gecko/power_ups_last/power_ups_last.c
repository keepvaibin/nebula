/* Power-ups never run out (Gecko census, MOD-PLAN M9): A power-up stays until another one replaces it or the player is hit. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

/* The game asks to change the power-up: back to none when its time runs out. Refused unless the player is hurt. */
static int32_t power_up(const NebulaModEventData* data, void* user) {
    const int now = game->value(NEBULA_VALUE_POWER_UP);
    (void)user;
    if (data->value == NEBULA_POWER_UP_NONE && now != NEBULA_POWER_UP_NONE && !game->value(NEBULA_VALUE_DAMAGED)) {
        return now;
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_POWER_UP, power_up, 0);
}
