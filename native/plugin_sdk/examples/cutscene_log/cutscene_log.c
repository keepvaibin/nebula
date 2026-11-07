/* Says each cutscene's name (UTF-8) as it starts and ends. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int32_t started(const NebulaModEventData* data, void* user) {
    (void)user;
    game->log(me, "cutscene started:");
    game->log(me, data->name);
    return data->value;
}

static int32_t ended(const NebulaModEventData* data, void* user) {
    (void)user;
    game->log(me, "cutscene ended:");
    game->log(me, data->name);
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_CUTSCENE_START, started, 0);
    game->subscribe(NEBULA_MOD_EVENT_CUTSCENE_END, ended, 0);
}
