/* Slow motion and frame advance (Gecko census, MOD-PLAN M9): Map button: half speed, held, then the game's speed. While held, d-pad down runs one step. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int mode;

static int32_t frame(const NebulaModEventData* data, void* user) {
    const uint32_t pressed = game->buttons_pressed();
    (void)user;
    if ((pressed & NEBULA_BUTTON_MAP) != 0) {
        mode = (mode + 1) % 3;
        game->set_game_speed(mode == 0 ? 1.0f : mode == 1 ? 0.5f : 0.0f);
    }
    if (mode == 2 && (pressed & NEBULA_BUTTON_DPAD_DOWN) != 0) {
        game->step_game(1);
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, frame, 0);
}
