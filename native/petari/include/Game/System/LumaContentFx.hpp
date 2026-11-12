#pragma once

// LumaCore's `content.fx` service (contract effects, section 3): particle effects a mod starts on an actor, followed,
// stopped and tinted; the failures made loud (section 2: Content.MissingEffect, Content.MissingJoint,
// Content.EffectEmpty); the harness facts fx.start, fx.end, fx.alive.

#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace content {
        namespace fx {
            // GameSystem::update, right after the services' frame start: the open effects' actors (dead: their effects
            // end; clipped: theirs stop making particles as the game's do), the matrices a turn composes, the check of
            // section 2 and the harness's fx.alive facts. One load while no mod has an effect open.
            void frame();
            // NebulaArena::destroy, before its memory goes: effects on actors in [pStart, pEnd) end ("scene"), never
            // touched again; all of them when the effect system itself goes. One load while no effect was ever made.
            void released(const void* pStart, const void* pEnd);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }
    }
}
