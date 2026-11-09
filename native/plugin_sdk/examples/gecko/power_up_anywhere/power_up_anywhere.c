/* Power-up anywhere (Gecko census, MOD-PLAN M9): D-pad: left Fire, right Ice, up Bee, down Boo; map button: back to normal. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    const uint32_t pressed = game->buttons_pressed();
    (void)user;
    if ((pressed & NEBULA_BUTTON_DPAD_LEFT) != 0) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_FIRE);
    } else if ((pressed & NEBULA_BUTTON_DPAD_RIGHT) != 0) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_ICE);
    } else if ((pressed & NEBULA_BUTTON_DPAD_UP) != 0) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_BEE);
    } else if ((pressed & NEBULA_BUTTON_DPAD_DOWN) != 0) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_BOO);
    } else if ((pressed & NEBULA_BUTTON_MAP) != 0) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_NONE);
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
