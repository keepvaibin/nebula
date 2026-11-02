#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTarget.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace J3DNativeTarget {
    void begin(const void* pKey, u16 width, u16 height, u32 clearRgba, u32 clearZ) {
        // State given before the pass (texture matrices, current matrices) goes first.
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_begin(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), width, height,
                            clearRgba, clearZ & 0xFFFFFF);
    }

    void end(const void* pKey, NebulaTargetChannels channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_end(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), static_cast< u32 >(channels));
    }
    void clear(u32 clearRgba, u32 clearZ, u32 channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_clear(pHost->user, clearRgba, clearZ & 0xFFFFFF, channels);
    }

    void clearFrame(u16 left, u16 top, u16 width, u16 height, u32 clearRgba, u32 clearZ, u32 channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_clear(pHost->user, left, top, width, height, clearRgba, clearZ & 0xFFFFFF, channels);
    }

    void capture(const void* pKey, u16 left, u16 top, u16 width, u16 height, NebulaTargetChannels channels, u32 flags) {
        J3DNativeShape::flushState();
        // A clear takes what the current writes allow.
        const NebulaPassState* pPass = J3DNativeState::pass();
        flags |= (pPass->color_write != 0 ? NEBULA_CAPTURE_CLEAR_COLOR : 0) | (pPass->alpha_write != 0 ? NEBULA_CAPTURE_CLEAR_ALPHA : 0) |
                 (pPass->depth_write != 0 ? NEBULA_CAPTURE_CLEAR_DEPTH : 0);
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_capture(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), left, top, width,
                              height, static_cast< u32 >(channels), flags);
    }
};  // namespace J3DNativeTarget

void J3DNativeTargetBegin(const void* pKey, u16 width, u16 height, u32 clearRgba, u32 clearZ) {
    J3DNativeTarget::begin(pKey, width, height, clearRgba, clearZ);
}

void J3DNativeTargetEnd(const void* pKey) {
    J3DNativeTarget::end(pKey);
}

void J3DNativeTargetEndChannels(const void* pKey, NebulaTargetChannels channels) {
    J3DNativeTarget::end(pKey, channels);
}

void J3DNativeTargetClear(u32 clearRgba, u32 clearZ, u32 channels) {
    J3DNativeTarget::clear(clearRgba, clearZ, channels);
}
