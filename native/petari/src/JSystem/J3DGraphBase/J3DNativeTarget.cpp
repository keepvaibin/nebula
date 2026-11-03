#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTarget.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace J3DNativeTarget {
    void begin(const void* pKey, u16 width, u16 height, u32 clearRgba) {
        // State given before the pass (texture matrices, current matrices) goes first.
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_begin(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)),
                            static_cast< u32 >(width * J3DNativeView::cUnitsAcross), static_cast< u32 >(height * J3DNativeView::cUnitsDown),
                            clearRgba);
    }

    void beginOverFrame(const void* pKey, u16 width, u16 height, u32 clearRgba, const void* pFrameKey,
                        NebulaTargetChannels channels, const void* pDepthKey) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_begin_over_frame(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)),
                                       static_cast< u32 >(width * J3DNativeView::cUnitsAcross),
                                       static_cast< u32 >(height * J3DNativeView::cUnitsDown), clearRgba,
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

    void clearFrame(u16 left, u16 top, u16 width, u16 height, u32 clearRgba, u32 channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_clear(pHost->user, static_cast< u32 >(left * J3DNativeView::cUnitsAcross), static_cast< u32 >(top * J3DNativeView::cUnitsDown),
                           static_cast< u32 >(width * J3DNativeView::cUnitsAcross), static_cast< u32 >(height * J3DNativeView::cUnitsDown), clearRgba, channels);
    }

    void framePass(const void* pKey, NebulaTargetChannels channels) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_pass(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), static_cast< u32 >(channels));
    }
    void take(const void* pKey) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_take(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)));
    }
};  // namespace J3DNativeTarget

void J3DNativeTargetBegin(const void* pKey, u16 width, u16 height, u32 clearRgba) {
    J3DNativeTarget::begin(pKey, width, height, clearRgba);
}

void J3DNativeTargetEnd(const void* pKey) {
    J3DNativeTarget::end(pKey);
}

void J3DNativeTargetClear(u32 clearRgba, u32 channels) {
    J3DNativeTarget::clear(clearRgba, channels);
}
