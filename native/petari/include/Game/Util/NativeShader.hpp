#pragma once

// A site's native material (nebula_game_api.h NebulaNativeMaterial; E-04): a shader compiled at build
// (runtime/src/render/materials), its constants, textures and fixed-function state, sent for the draws that follow.

#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <revolution/types.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NativeShader {
    // Blend presets for hand-written sites.
    enum Blend { BLEND_OPAQUE, BLEND_ALPHA, BLEND_ADD_ALPHA, BLEND_SUBTRACT, BLEND_ADD };

    // `depthFunc` NEBULA_COMPARE_* (NEBULA_COMPARE_ALWAYS with no write: depth off).
    inline void init(NebulaNativeMaterial& rMaterial, NebulaShaderId shader, Blend blend, u8 depthFunc,
                     bool depthWrite, u8 cull) {
        memset(&rMaterial, 0, sizeof(rMaterial));
        rMaterial.shader = shader;
        rMaterial.blend_op = blend == BLEND_OPAQUE ? NEBULA_BLEND_REPLACE
                             : blend == BLEND_SUBTRACT ? NEBULA_BLEND_SUBTRACT
                                                       : NEBULA_BLEND_ADD;
        rMaterial.blend_src = blend == BLEND_OPAQUE || blend == BLEND_SUBTRACT || blend == BLEND_ADD
                                  ? NEBULA_FACTOR_ONE
                                  : NEBULA_FACTOR_SRC_ALPHA;
        rMaterial.blend_dst = blend == BLEND_ALPHA ? NEBULA_FACTOR_INV_SRC_ALPHA
                              : blend == BLEND_OPAQUE ? NEBULA_FACTOR_ZERO
                                                      : NEBULA_FACTOR_ONE;
        rMaterial.depth_test = depthFunc != NEBULA_COMPARE_ALWAYS || depthWrite ? 1 : 0;
        rMaterial.depth_func = depthFunc;
        rMaterial.depth_write = depthWrite ? 1 : 0;
        rMaterial.cull = cull;
        rMaterial.color_write = 2;  // as it was (0 off, 1 on, 2 kept)
        rMaterial.alpha_write = 2;
    }

    // Texture `slot` (the shader's t<slot>): the texture's image, size, format, wrap and filtering.
    inline void setTexture(NebulaNativeMaterial& rMaterial, u32 slot, const JUTTexture* pTexture) {
        rMaterial.textures[slot].image = reinterpret_cast< uintptr_t >(pTexture->mImage);
        rMaterial.textures[slot].width = static_cast< u16 >(pTexture->getWidth());
        rMaterial.textures[slot].height = static_cast< u16 >(pTexture->getHeight());
        rMaterial.textures[slot].format = static_cast< u8 >(pTexture->getFormat() & 0x0F);
        rMaterial.textures[slot].repeat_s = pTexture->mWrapS;
        rMaterial.textures[slot].repeat_t = pTexture->mWrapT;
        // The texture's filters as it loads them (its resource's, GXTexFilter: near 0, linear 1, then by mip level
        // near/linear: 2-3 nearest level, 4-5 between levels).
        const u8 minType = pTexture->mMinType;
        rMaterial.textures[slot].mag_linear = pTexture->mMagType != 0 ? 1 : 0;
        rMaterial.textures[slot].min_linear = minType == 1 || minType == 3 || minType == 5 ? 1 : 0;
        rMaterial.textures[slot].mip_filter = minType < 2 ? 0 : minType < 4 ? 1 : 2;
        rMaterial.textures[slot].anisotropy = pTexture->mTIMG->mMaxAnisotropy;
        rMaterial.textures[slot].edge_lod = pTexture->mTIMG->mDoEdgeLod ? 1 : 0;
        rMaterial.textures[slot].lod_bias = pTexture->mLodBias / 100.0f;
        rMaterial.textures[slot].min_lod = static_cast< s16 >(pTexture->mMinLod) / 8.0f;
        rMaterial.textures[slot].max_lod = static_cast< s16 >(pTexture->mMaxLod) / 8.0f;
    }

    // Constants from offset `at` (floats): an RGBA colour as 0-1 values.
    inline void setColor(NebulaNativeMaterial& rMaterial, u32 at, u8 r, u8 g, u8 b, u8 a) {
        rMaterial.constants[at] = r / 255.0f;
        rMaterial.constants[at + 1] = g / 255.0f;
        rMaterial.constants[at + 2] = b / 255.0f;
        rMaterial.constants[at + 3] = a / 255.0f;
    }

    inline void send(const NebulaNativeMaterial& rMaterial) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &rMaterial);
    }
}  // namespace NativeShader
