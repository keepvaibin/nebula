/* Wide angle of view (Gecko census, MOD-PLAN M9): The camera draws 60 degrees from top to bottom. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->set_camera(0, 0, 60.0f);
}
