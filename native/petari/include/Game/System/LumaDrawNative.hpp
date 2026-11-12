#pragma once

// LumaCore's `draw.native` service (plan 2.4 layer 9; contract draw 16; native/include/luma_draw_native.h): the mods'
// own GPU commands at the draw points, as NEBULA_PASS_NATIVE passes whose functions the host calls on its render thread.
// The harness fact draw.native.<point>.

#include <luma_service_provider.h>

namespace luma {
    namespace draw {
        namespace native {
            // luma::draw::point, first: each function set at `point` becomes a pass. One load while none is set.
            void point(u32 point);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }  // namespace native
    }  // namespace draw
}  // namespace luma
