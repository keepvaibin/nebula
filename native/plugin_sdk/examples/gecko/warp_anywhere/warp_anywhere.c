/* Warp anywhere (Gecko census, MOD-PLAN M9): Map button and d-pad up: to the Good Egg Galaxy's first star. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    (void)user;
    if ((game->buttons_held() & NEBULA_BUTTON_MAP) != 0 && (game->buttons_pressed() & NEBULA_BUTTON_DPAD_UP) != 0) {
        game->change_stage("EggStarGalaxy", 1);
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
