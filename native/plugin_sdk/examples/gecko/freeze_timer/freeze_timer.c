/* Freeze timers (Gecko census, MOD-PLAN M9): Comet countdowns never run down. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->pause_timer(1);
}
