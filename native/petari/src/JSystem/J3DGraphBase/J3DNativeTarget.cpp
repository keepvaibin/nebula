#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTarget.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace J3DNativeTarget {
    void begin(const void* pKey, f32 across, f32 down, u32 clearRgba) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_begin(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)),
                            static_cast< u32 >(across * NEBULA_FRAME_UNITS), static_cast< u32 >(down * NEBULA_FRAME_UNITS),
                            clearRgba);
    }

    void beginPixels(const void* pKey, u32 width, u32 height, u32 clearRgba) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_begin_pixels(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), width, height,
                                   clearRgba);
    }

    void beginOverFrame(const void* pKey, f32 across, f32 down, u32 clearRgba, const void* pFrameKey,
                        NebulaTargetChannels channels, const void* pDepthKey) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_begin_over_frame(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)),
                                       static_cast< u32 >(across * NEBULA_FRAME_UNITS),
                                       static_cast< u32 >(down * NEBULA_FRAME_UNITS), clearRgba,
                                       static_cast< u64 >(reinterpret_cast< uintptr_t >(pFrameKey)), static_cast< u32 >(channels),
                                       static_cast< u64 >(reinterpret_cast< uintptr_t >(pDepthKey)));
    }

    void end(const void* pKey, NebulaTargetChannels channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_end(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), static_cast< u32 >(channels));
    }
    void clear(u32 clearRgba, u32 channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_clear(pHost->user, clearRgba, channels);
    }

    void clearFrame(u32 clearRgba, u32 channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_clear(pHost->user, clearRgba, channels);
    }

    void framePass(const void* pKey) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_pass(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)));
    }
    void take(const void* pKey) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_take(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)));
    }
};  // namespace J3DNativeTarget

void J3DNativeTargetBeginPixels(const void* pKey, u32 width, u32 height, u32 clearRgba) {
    J3DNativeTarget::beginPixels(pKey, width, height, clearRgba);
}

void J3DNativeTargetEnd(const void* pKey) {
    J3DNativeTarget::end(pKey);
}

void J3DNativeTargetClear(u32 clearRgba, u32 channels) {
    J3DNativeTarget::clear(clearRgba, channels);
}
