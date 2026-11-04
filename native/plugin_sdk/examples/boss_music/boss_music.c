/* Boss music: ten seconds into a level a track of the mod's starts, and ten seconds later it fades. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;
static int frames;

static int32_t stage_start(const NebulaModEventData* data, void* user) {
    (void)user;
    frames = 0;
    return data->value;
}

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    (void)user;
    ++frames;
    if (frames == 600) {
        game->log(me, game->play_music("MyBossTheme") != 0 ? "started" : "not started");
    }
    if (frames == 1200) {
        game->stop_music(60);
        game->log(me, "stopped");
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->add_music("MyBossTheme", "AudioRes/Stream/MyBossTheme.wav");
    game->subscribe(NEBULA_MOD_EVENT_STAGE_START, stage_start, 0);
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
