/* Moon jump (Gecko census, MOD-PLAN M9): Hold jump to rise. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

/* Up is away from gravity, wherever the player stands. */
static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    NebulaActor* player = game->player();
    float down[3], velocity[3];
    (void)user;
    if (player == 0 || (game->buttons_held() & NEBULA_BUTTON_JUMP) == 0) {
        return data->value;
    }
    game->get_gravity(player, down);
    game->get_velocity(player, velocity);
    velocity[0] = -down[0] * 25.0f;
    velocity[1] = -down[1] * 25.0f;
    velocity[2] = -down[2] * 25.0f;
    game->set_velocity(player, velocity);
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
