/* Low gravity (Gecko census, MOD-PLAN M9): Everything falls at half the pull. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->set_gravity_scale(0.5f);
}
