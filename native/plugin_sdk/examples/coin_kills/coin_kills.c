/* A coin kills. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t coin(const NebulaModEventData* data, void* user) {
    (void)data;
    (void)user;
    game->log(me, "a coin");
    game->kill_player();
    return 0;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_COIN, coin, 0);
}
