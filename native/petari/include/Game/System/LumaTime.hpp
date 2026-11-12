#pragma once

// LumaCore's `time` service (contract services-time): the frame and world step counts, the real clocks, the world
// clock's rate, hold and step with the sound following, timers as events, and the mods' frame and step handlers.

#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace time {
        // GameSystem::frameLoop, right after the frame's wait: the world steps this frame runs (services-time 3, 4).
        // 1 while no mod has set a rate, a hold or steps: one load.
        u32 beginFrame();
        // GameSystem::updateSceneController: false when this frame holds the world (its first update runs no step).
        bool isStepAllowed();
        // GameSystem::frameLoop, before each extra world step (a rate above real time): false once this frame's steps
        // changed what the game is doing (it holds the scene, a scene change or reset is asked, a movie or the skip
        // began): the rest of the frame's steps are dropped, as the clock is suspended there (services-time 4).
        bool canStepAgain();
        // GameSystem::updateSceneController, right before the scene's update: counts the world step and runs the mods'
        // step handlers.
        void beginStep();
        // The rate the game's sound follows now, in thousandths (1000 real time; 0 the world's sound held). Any thread
        // (the sound engine's tick reads it on the audio clock's thread).
        u32 soundRate();

        // LumaServices' frame start: before the events, the due timers' events are posted; after them, the frame
        // handlers run (services-registry 6.1, services-time 3 step 3).
        void framePost();
        void frameHandlers();

        // The Mods page (J, plan 6.2; services-time 1 item 10), game thread: the mod's own rate and hold (false: time
        // does not know it), and the player's "Let go": its rate, hold and owed steps cleared from the next frame, with
        // the page writing the mod log's Note Time.LetGo.
        bool clockOf(const LumaMod* pMod, u32* pRate, bool* pHolds);
        void letGo(const LumaMod* pMod);

        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();

        // The host's UTC clock (the module export luma_services_facts, before the first mod): nanoseconds since 1970 UTC.
        void setUtcClock(int64_t (*pUtcNs)());
    }
}
