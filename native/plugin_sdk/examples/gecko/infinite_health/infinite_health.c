/* Infinite health (Gecko census, MOD-PLAN M9): No hit takes health. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

/* Returning 0 refuses the hit. */
static int32_t damage(const NebulaModEventData* data, void* user) {
    (void)data;
    (void)user;
    return 0;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_PLAYER_DAMAGE, damage, 0);
}
