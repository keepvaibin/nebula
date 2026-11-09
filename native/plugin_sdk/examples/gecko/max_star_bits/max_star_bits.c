/* Max star bits and coins (Gecko census, MOD-PLAN M9): D-pad up fills star bits, d-pad down adds 50 coins. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    const uint32_t pressed = game->buttons_pressed();
    (void)user;
    if ((pressed & NEBULA_BUTTON_DPAD_UP) != 0) {
        game->set_value(NEBULA_SET_ADD_STAR_BITS, 9999 - game->value(NEBULA_VALUE_STAR_BITS));
    }
    if ((pressed & NEBULA_BUTTON_DPAD_DOWN) != 0) {
        game->set_value(NEBULA_SET_ADD_COINS, 50);
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
