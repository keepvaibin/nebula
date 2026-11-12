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
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }  // namespace movie
}  // namespace luma
