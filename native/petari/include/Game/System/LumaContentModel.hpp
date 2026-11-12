#pragma once

// LumaCore's `content.model` service (plan 3.2; contract models 7): a model drawn with an actor of the game, following
// its base, a joint or its whole skeleton; shown, hidden, detached; the player's parts listed. The harness facts
// model.attach, model.end, model.pose.

#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace content {
        namespace model {
            // GameSystem::update, right after the services' frame start: attached models whose actor died end, and
            // the harness's model.pose facts. One load while no mod has a model attached.
            void frame();
            // NebulaArena::destroy, before its memory goes: models made in [pStart, pEnd), or on actors there, end
            // ("scene"), never touched again. One load while no model was ever attached.
            void released(const void* pStart, const void* pEnd);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }
    }
}
