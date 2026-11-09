/* 99 lives (Gecko census, MOD-PLAN M9): Lives stay at 99. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    const int lives = game->value(NEBULA_VALUE_LIVES);
    (void)user;
    if (lives < 99) {
        game->set_value(NEBULA_SET_ADD_LIVES, 99 - lives);
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
