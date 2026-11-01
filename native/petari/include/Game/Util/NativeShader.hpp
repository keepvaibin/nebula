#pragma once

// A site's native material (nebula_game_api.h NebulaNativeMaterial; E-04): a shader compiled at build
// (runtime/src/render/materials), its constants, textures and fixed-function state, sent for the draws that follow.

#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/JUtility/JUTPalette.hpp>
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
        // A colour-indexed texture's palette (the texture's own, else the one in its resource).
        const JUTPalette* pPalette = pTexture->mPalette != NULL ? pTexture->mPalette : pTexture->mEmbPalette;
        const u32 format = pTexture->getFormat() & 0x0F;
        if (pPalette != NULL && (format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2)) {
            rMaterial.textures[slot].palette = reinterpret_cast< uintptr_t >(pPalette->mColorTable);
            rMaterial.textures[slot].palette_entries = pPalette->mLutNum;
            rMaterial.textures[slot].palette_format = pPalette->mFormat;
        }
    }

    // Texture `slot` from a texture object the site set up (J3DNativeTexture::init: an image it made or captured).
    inline void setTexObj(NebulaNativeMaterial& rMaterial, u32 slot, const GXTexObj* pObj) {
        void* pImage;
        u16 width, height;
        GXTexFmt format;
        GXTexWrapMode wrapS, wrapT;
        GXBool mipmap;
        J3DNativeTexture::getAll(pObj, &pImage, &width, &height, &format, &wrapS, &wrapT, &mipmap);
        GXTexFilter minFilter, magFilter;
        f32 minLod, maxLod, lodBias;
        GXBool biasClamp, edgeLod;
        GXAnisotropy maxAniso;
        J3DNativeTexture::getLODAll(pObj, &minFilter, &magFilter, &minLod, &maxLod, &lodBias, &biasClamp, &edgeLod,
                                    &maxAniso);
        rMaterial.textures[slot].image = reinterpret_cast< uintptr_t >(pImage);
        rMaterial.textures[slot].width = width;
        rMaterial.textures[slot].height = height;
        rMaterial.textures[slot].format = static_cast< u8 >(format & 0x0F);
        rMaterial.textures[slot].repeat_s = static_cast< u8 >(wrapS);
        rMaterial.textures[slot].repeat_t = static_cast< u8 >(wrapT);
        rMaterial.textures[slot].mag_linear = magFilter != GX_NEAR ? 1 : 0;
        rMaterial.textures[slot].min_linear = minFilter == 1 || minFilter == 3 || minFilter == 5 ? 1 : 0;
        rMaterial.textures[slot].mip_filter = minFilter < 2 ? 0 : minFilter < 4 ? 1 : 2;
        rMaterial.textures[slot].anisotropy = static_cast< u8 >(maxAniso);
        rMaterial.textures[slot].edge_lod = edgeLod ? 1 : 0;
        rMaterial.textures[slot].lod_bias = lodBias;
        rMaterial.textures[slot].min_lod = minLod;
        rMaterial.textures[slot].max_lod = maxLod;
    }

    // Constants from offset `at` (floats): an RGBA colour as 0-1 values.
    inline void setColor(NebulaNativeMaterial& rMaterial, u32 at, u8 r, u8 g, u8 b, u8 a) {
        rMaterial.constants[at] = r / 255.0f;
        rMaterial.constants[at + 1] = g / 255.0f;
        rMaterial.constants[at + 2] = b / 255.0f;
        rMaterial.constants[at + 3] = a / 255.0f;
    }

    // Depth: tested by `func` (NEBULA_COMPARE_*, GX's order) when `test`, written when `write` (GXSetZMode's terms).
    inline void setDepth(NebulaNativeMaterial& rMaterial, bool test, u8 func, bool write) {
        rMaterial.depth_test = test ? 1 : 0;
        rMaterial.depth_func = test ? func : static_cast< u8 >(NEBULA_COMPARE_ALWAYS);
        rMaterial.depth_write = test && write ? 1 : 0;
    }

    // Blending as GXSetBlendMode's blend: source and destination factors (NEBULA_FACTOR_*, GX's order).
    inline void setBlend(NebulaNativeMaterial& rMaterial, u8 src, u8 dst) {
        rMaterial.blend_op = NEBULA_BLEND_ADD;
        rMaterial.blend_src = src;
        rMaterial.blend_dst = dst;
    }

    // A material whose shader reads the register file (j3d_material.hlsli's first 84 floats: colour and constant
    // registers, channel colours, indirect matrices, fog, alpha references) in its first constants: they start as the
    // game's register file and follow its changes (the game's state changes of those values resend them).
    inline void sendWithRegisters(NebulaNativeMaterial& rMaterial) {
        memcpy(rMaterial.constants, J3DNativeMaterial::registers(), 84 * sizeof(f32));
        rMaterial.constants_only |= 2;
        J3DNativeShape::flushState();
        J3DNativeMaterial::watch(&rMaterial);
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &rMaterial);
    }

    inline void send(const NebulaNativeMaterial& rMaterial) {
        J3DNativeShape::flushState();
        J3DNativeMaterial::watch(&rMaterial);
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &rMaterial);
        J3DNativeMaterial::dumpTag(rMaterial.shader);
    }
}  // namespace NativeShader
