#pragma once

// A site's native material (nebula_game_api.h NebulaNativeMaterial; E-04): a shader compiled at build
// (runtime/src/render/materials), its constants, textures and fixed-function state, sent for the draws that follow.

#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
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
        for (u32 i = 0; i < 8; i++) {
            rMaterial.texgen_post[i] = 61;  // the identity post-transform 
        }
    }

    // Texture `slot` (the shader's t<slot>): the texture (its record has size and format), wrap and filtering.
    inline void setTexture(NebulaNativeMaterial& rMaterial, u32 slot, const JUTTexture* pTexture) {
        rMaterial.textures[slot].image = reinterpret_cast< uintptr_t >(pTexture->mImage);
        rMaterial.textures[slot].repeat_s = pTexture->mWrapS;
        rMaterial.textures[slot].repeat_t = pTexture->mWrapT;
        // The texture's filters as it loads them (its resource's, u8: near 0, linear 1, then by mip level
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

    // Texture `slot` from a texture object the site set up (J3DNativeTexture::init: an image it made or captured).
    inline void setTexObj(NebulaNativeMaterial& rMaterial, u32 slot, const J3DNativeTexObj* pObj) {
        const u8 minFilter = pObj->mMinFilter;
        rMaterial.textures[slot].image = reinterpret_cast< uintptr_t >(pObj->mImage);
        rMaterial.textures[slot].repeat_s = pObj->mWrapS;
        rMaterial.textures[slot].repeat_t = pObj->mWrapT;
        rMaterial.textures[slot].mag_linear = pObj->mMagFilter != NEBULA_FILTER_NEAREST ? 1 : 0;
        rMaterial.textures[slot].min_linear = minFilter == 1 || minFilter == 3 || minFilter == 5 ? 1 : 0;
        rMaterial.textures[slot].mip_filter = minFilter < 2 ? 0 : minFilter < 4 ? 1 : 2;
        rMaterial.textures[slot].anisotropy = pObj->mMaxAniso;
        rMaterial.textures[slot].edge_lod = pObj->mEdgeLod;
        rMaterial.textures[slot].lod_bias = pObj->mLodBias;
        rMaterial.textures[slot].min_lod = pObj->mMinLod;
        rMaterial.textures[slot].max_lod = pObj->mMaxLod;
    }

    // Constants from offset `at` (floats): an RGBA colour as 0-1 values.
    inline void setColor(NebulaNativeMaterial& rMaterial, u32 at, u8 r, u8 g, u8 b, u8 a) {
        rMaterial.constants[at] = r / 255.0f;
        rMaterial.constants[at + 1] = g / 255.0f;
        rMaterial.constants[at + 2] = b / 255.0f;
        rMaterial.constants[at + 3] = a / 255.0f;
    }

    // Depth: tested by `func` (NEBULA_COMPARE_*) when `test`, written when `write`.
    inline void setDepth(NebulaNativeMaterial& rMaterial, bool test, u8 func, bool write) {
        rMaterial.depth_test = test ? 1 : 0;
        rMaterial.depth_func = test ? func : static_cast< u8 >(NEBULA_COMPARE_ALWAYS);
        rMaterial.depth_write = test && write ? 1 : 0;
    }

    // Blending: source and destination factors (NEBULA_FACTOR_*).
    inline void setBlend(NebulaNativeMaterial& rMaterial, u8 src, u8 dst) {
        rMaterial.blend_op = NEBULA_BLEND_ADD;
        rMaterial.blend_src = src;
        rMaterial.blend_dst = dst;
    }

    inline void send(const NebulaNativeMaterial& rMaterial) {
        J3DNativeShape::flushState();
        J3DNativeMaterial::watch(&rMaterial);
        J3DNativeState::sendMaterial(&rMaterial);
    }
}  // namespace NativeShader
