#pragma once

// LumaCore's `content.model` service (docs/modding/content/models.md section 7): a model drawn with an actor of the
// game, following its base, a joint or its whole skeleton; shown, hidden, detached; the player's parts listed. The
// harness facts model.attach, model.end, model.pose.

#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_types.h>

class LiveActor;

namespace luma {
    namespace content {
        namespace model {
            // GameSystem::update, right after the services' frame start: attached models whose actor died end, and
            // the harness's model.pose facts. One load while no mod has a model attached.
            void frame();
            // NebulaArena::destroy, before its memory goes: models made in [pStart, pEnd), or on actors there, end
            // ("scene"), never touched again. One load while no model was ever attached.
            void released(const void* pStart, const void* pEnd);
            // content.fx's start_on_model (effects 3): the actor that draws the mod's open model `handle` (its part; the
            // actor it is attached to while neither the model nor the placeholder is in the store); null: no open
            // handle of the mod. Game thread.
            LiveActor* actorOf(const LumaMod* pMod, LumaHandle handle);
            // anim's play_on_model (animation 7): the part that draws the mod's open model `handle`, null while nothing
            // shows; *pOpen: the handle is one of the mod's open ones. Game thread.
            LiveActor* partOf(const LumaMod* pMod, LumaHandle handle, bool* pOpen);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }
    }
}
