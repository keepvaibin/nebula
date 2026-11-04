/* A movie of the mod's, ten seconds into a level, stopped after five. */
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
    if (++frames == 600) {
        game->log(me, game->play_movie("MovieData/MyMovie.thp") != 0 ? "playing" : "no such movie");
    }
    if (frames == 900 && game->is_movie_playing() != 0) {
        game->stop_movie();
        game->log(me, "stopped");
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_START, stage_start, 0);
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
