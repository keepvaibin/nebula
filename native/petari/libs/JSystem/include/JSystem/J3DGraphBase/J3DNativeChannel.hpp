#pragma once

#include <NebulaGpu.h>
#include <revolution/types.h>

// A colour channel's lighting as the renderer takes it (nebula_game_api.h NEBULA_STATE_CHANNEL): the diffuse function (0 none, 1 signed, 2 clamped; none with specular attenuation) and the
// attenuation (0 none, 1 specular, 2 directional, 3 spot) from GpuDiffuseFn and GpuAttnFn.
// tools/dev/gen_j3d_materials.py native_channel converts dumped records the same way.
namespace J3DNativeChannel {
    inline u8 attenuation(u32 attn) {
        return static_cast< u8 >(attn == GPU_AF_SPEC ? 1 : attn == GPU_AF_SPOT ? 3 : 2);
    }
    inline u8 diffuse(u32 diffuseFn, u32 attn) {
        return static_cast< u8 >(attn == GPU_AF_SPEC ? 0 : diffuseFn & 3);
    }
}  // namespace J3DNativeChannel
