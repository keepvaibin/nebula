#pragma once

// LumaCore's `ui` service (docs/modding/services/ui.md): a mod's or the game's screen layout shown
// over the game by the game's layout drawer, its panes found by name, their text and visibility set. The harness facts
// ui.open, ui.pane, ui.text, ui.screen, ui.end.

#include <luma_service_provider.h>
#include <luma_services.h>

namespace luma {
    namespace ui {
        // GameSystem::update, right after the services' frame start: the harness's ui.screen facts. One load while no
        // mod has a screen open.
        void frame();
        // GameSystem::update, before NebulaMenuFocus::update: a mod's shown screen with a menu is on top (as frame()
        // reads it), so it takes this frame's pad and the game's menus under it take none.
        bool takesControls();
        // NebulaArena::destroy, before its memory goes: screens made in [pStart, pEnd) end ("scene"), never touched
        // again. One load while no screen was ever made.
        void released(const void* pStart, const void* pEnd);
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }
}
