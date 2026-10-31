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

    void end(const void* pKey, NebulaTargetTexture kept) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->target_end(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)), static_cast< u8 >(kept));
    }
};  // namespace J3DNativeTarget

void J3DNativeTargetBegin(const void* pKey, u16 width, u16 height, u32 clearRgba, u32 clearZ) {
    J3DNativeTarget::begin(pKey, width, height, clearRgba, clearZ);
}

void J3DNativeTargetEnd(const void* pKey, NebulaTargetTexture kept) {
    J3DNativeTarget::end(pKey, kept);
}
