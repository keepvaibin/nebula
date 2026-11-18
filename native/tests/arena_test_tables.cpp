// SPDX-License-Identifier: GPL-3.0-only
// What NebulaArena::destroy tells (the tables, textures and file views it lets go of), for a test that uses the game's
// arenas and destroys none. arena_tests.cpp keeps its own, which record what they are told.
#include "Game/System/NebulaArena.hpp"

#include <stdint.h>

namespace lumacore {
    void released(const void*, const void*) {}
}  // namespace lumacore
namespace luma {
    namespace content {
        namespace fx {
            void released(const void*, const void*) {}
        }  // namespace fx
        namespace model {
            void released(const void*, const void*) {}
        }  // namespace model
    }  // namespace content
    namespace draw {
        void released(const void*, const void*) {}
        namespace inst {
            void released(const void*, const void*) {}
        }  // namespace inst
        namespace view {
            void released(const void*, const void*) {}
        }  // namespace view
    }  // namespace draw
    namespace ui {
        void released(const void*, const void*) {}
    }  // namespace ui
    namespace anim {
        void released(const void*, const void*) {}
    }  // namespace anim
    namespace player {
        void released(const void*, const void*) {}
    }  // namespace player
}  // namespace luma
namespace RenderTexture {
    void forget(uintptr_t, uintptr_t) {}
}  // namespace RenderTexture
namespace GameAssets {
    void unloadArena(const NebulaArena*) {}
}  // namespace GameAssets
