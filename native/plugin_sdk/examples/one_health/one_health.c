/* One-health: any hit is the last one. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    (void)user;
    if (game->value(NEBULA_VALUE_LIFE) > 1) {
        game->set_value(NEBULA_SET_LIFE, 1);
        game->log(me, "one health");
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
