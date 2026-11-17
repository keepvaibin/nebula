#pragma once

// LumaCore's `events` service (docs/modding/reference/services/events.md): the game's events (NebulaMods::fire) told to
// the mods that watch them at the start of the next frame.

#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace events {
        // NebulaMods::dispatch, for an event some mod watches (NebulaMods::sWanted): kept for each watching mod and its
        // node posted. Any thread (the game raises its events on the game thread).
        void raise(u32 event, s32 value, void* pActor, const char* pName);
        // GameSystemSceneController, after the scene's memory went: an actor raised before it is given as 0. Game thread.
        void sceneGone();
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }  // namespace events
}  // namespace luma
