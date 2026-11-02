#pragma once

#include <NebulaGpu.h>
#include <revolution/types.h>

// A colour channel's lighting as the renderer takes it (nebula_game_api.h NebulaMaterialProgram::channel,
// NEBULA_STATE_CHANNEL): the diffuse function (0 none, 1 signed, 2 clamped; none with specular attenuation) and the
// attenuation (0 none, 1 specular, 2 directional, 3 spot) from GpuDiffuseFn and GpuAttnFn.
// tools/dev/gen_j3d_materials.py native_channel converts dumped records the same way.
namespace J3DNativeChannel {
    inline u8 attenuation(u32 attn) {
        return static_cast< u8 >(attn == GPU_AF_SPEC ? 1 : attn == GPU_AF_SPOT ? 3 : 2);
    }
    inline u8 diffuse(u32 diffuseFn, u32 attn) {
        return static_cast< u8 >(attn == GPU_AF_SPEC ? 0 : diffuseFn & 3);
    }

    // A record's four channels written with GX's values, in the renderer's encoding; channel[0].reserved[0] marks it.
    template < typename Program >
    inline void toNative(Program& rProgram) {
        for (u32 c = 0; c < 4; c++) {
            const u32 attn = rProgram.channel[c].attenuation;
            rProgram.channel[c].diffuse = diffuse(rProgram.channel[c].diffuse, attn);
            rProgram.channel[c].attenuation = attenuation(attn);
        }
        rProgram.channel[0].reserved[0] = 1;
    }
}  // namespace J3DNativeChannel
