#pragma once

// LumaCore's `movie` service (plan 3.10; contracts movies, references 6): a mod's own cooked movie (NMOV, AV1)
// played by the scene's movie player, MoviePlayerSimple, as the game plays its own. The harness facts movie.play and
// movie.end.

#include <luma_service_provider.h>

namespace luma {
    namespace movie {
        // GameSystem::update, after the services' frame start: the mod's movie ended (its end, the scene's, another
        // movie). One load while no mod's movie plays.
        void frame();
        // GameScene::draw2D while a movie plays, over it: the line of `<movie>.subs` (the cook's table beside the movie:
        // text key, from, to in seconds) at the movie's frame, the mod's text in the played language (plan 3.10 layer
        // 4). Any movie the scene's player plays: a mod's own and a game movie a mod replaced. One load with none.
        void drawSubtitles();
        // Whether `<movie>.subs` sits beside the movie at pMovieName ("/MovieData/PrologueA.thp"): a mod's movie in that
        // place carries its own lines (none when the table is empty), so the game's own are not shown over it.
        bool hasSubtitles(const char* pMovieName);
        // NebulaCutsceneSkip, while the scene's player plays: false for a mod's movie whose cook says it may not be
        // skipped (`"skip": false`, NMOV flags 0x2C without 2); the game's own movies skip as ever.
        bool isSkippable();
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }  // namespace movie
}  // namespace luma
