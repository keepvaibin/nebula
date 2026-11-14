#pragma once

// LumaCore's `anim` service (docs/modding/content/animation.md section 7): a clip of an actor's model, or of a mod's
// attached model, played as the game plays its own, from a frame, at a rate, once or looping; its frame read; marks
// and its end as events. The harness facts anim.play, anim.frame, anim.mark, anim.end.

#include <luma_service_provider.h>
#include <luma_services.h>

namespace luma {
    namespace anim {
        // GameSystem::update, right after the services' frame start: plays whose clip ended, was replaced or whose
        // actor died end; marks the clips passed last frame are given. One load while no mod has a play going.
        void frame();
        // NebulaArena::destroy, before its memory goes: plays on actors in [pStart, pEnd) end ("scene"), never touched
        // again. One load while no play is going.
        void released(const void* pStart, const void* pEnd);
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }
}
