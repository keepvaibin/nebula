#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "Game/System/NebulaArena.hpp"
#include <nebula_game_api.h>
#include <nebula_cooked_material.h>
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);
extern "C" void nebula_log(const char* format, ...);

// A material: its program and its values (nebula_game_api.h).

namespace {
    // A J3D material's values, whatever its shader reads of them. Slots (tools/dev/j3d_compact.py SLOTS): where each
    // lies in these, four floats each. Its shader's constants are the slots its layout names, in order.
    const u32 cSlotCount = 30;
    const u8 cSlotAt[cSlotCount] = {4,  8,  12, 16, 20, 24, 28, 32, 36, 40,  44,  48,  52,  56,  60,
                                    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120};
    enum {
        SLOT_REGISTER1 = 0,
        SLOT_MAT_COLOR0 = 7,
        SLOT_AMB_COLOR0 = 9,
        SLOT_FOG = 17,
        SLOT_FOG_COLOR = 18,
        SLOT_TEX_SCALE0 = 20,
        SLOT_OVR = 28,
        SLOT_OVR_COORD = 29,
    };
    f32 sValues[124];
    u32 sLayout = 0;

    // The ambient colours now: a material with its own sets them, an actor's light sets them after that.
    f32 sAmbient[2][4];

    // The native material the draws use now (J3D: sCurrent, its program and values; or a site's own), and the state
    // changes made since it was sent (replayed should the old path take over its draws).
    enum CurrentKind {
        CURRENT_NONE,
        CURRENT_J3D,
        CURRENT_SITE,
    };
    CurrentKind sCurrentKind = CURRENT_NONE;
    NebulaNativeMaterial sCurrent;
    const NebulaCookedMaterial* sCurrentCooked = NULL;
    u8 sCurrentPass = 0;

    void setColor8(f32* pOut, NebulaColor8 color) {
        pOut[0] = color.r / 255.0f;
        pOut[1] = color.g / 255.0f;
        pOut[2] = color.b / 255.0f;
        pOut[3] = color.a / 255.0f;
    }

    void setColor(f32* pOut, const u8* pColor, u32 count) {
        for (u32 c = 0; c < count; c++) {
            pOut[c] = pColor[c] / 255.0f;
        }
    }

    const f32 cDefaultIndirect[2][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

    // A material's constants (j3d_material.hlsli MaterialConstants) from its values. What a material has none of is the
    // neutral value: white colours, no fog, the plain indirect matrix.
    void fillConstants(f32* pC, const J3DMaterialSetup& rSetup, const J3DMaterialValues& rValues) {
        const u16 loads = rSetup.loads;
        for (u32 c = 0; c < 4; c++) {
            pC[c] = 1.0f;
        }
        for (u32 r = 0; r < 3; r++) {
            for (u32 c = 0; c < 4; c++) {
                pC[4 + r * 4 + c] = (loads & J3D_LOADS_REGISTER_COLOR) != 0 ? rValues.register_color[r][c] / 255.0f : 1.0f;
            }
        }
        for (u32 k = 0; k < 4; k++) {
            for (u32 c = 0; c < 4; c++) {
                pC[16 + k * 4 + c] = (loads & J3D_LOADS_KONST_COLOR) != 0 ? rValues.konst_color[k][c] / 255.0f : 1.0f;
            }
        }
        for (u32 ch = 0; ch < 2; ch++) {
            for (u32 c = 0; c < 4; c++) {
                pC[32 + ch * 4 + c] = (loads & J3D_LOADS_CHANNELS) != 0 ? rValues.material_color[ch][c] / 255.0f : 1.0f;
                if ((loads & J3D_LOADS_AMBIENT) != 0) {
                    sAmbient[ch][c] = rValues.ambient_color[ch][c] / 255.0f;
                }
                pC[40 + ch * 4 + c] = sAmbient[ch][c];
            }
        }
        for (u32 m = 0; m < 3; m++) {
            f32 scale = 1.0f;
            for (s32 e = rValues.ind_matrix[m].scale; e > 0; e--) {
                scale *= 2.0f;
            }
            for (s32 e = rValues.ind_matrix[m].scale; e < 0; e++) {
                scale *= 0.5f;
            }
            for (u32 row = 0; row < 2; row++) {
                for (u32 c = 0; c < 3; c++) {
                    pC[48 + (m * 2 + row) * 4 + c] =
                        (loads & J3D_LOADS_INDIRECT) != 0 ? rValues.ind_matrix[m].m[row][c] * scale : cDefaultIndirect[row][c];
                }
                pC[48 + (m * 2 + row) * 4 + 3] = 0.0f;
            }
        }
        if ((loads & J3D_LOADS_FOG) != 0) {
            pC[72] = rValues.fog.start;
            pC[73] = rValues.fog.end;
            pC[74] = rValues.fog.near_z;
            pC[75] = rValues.fog.far_z;
            setColor(pC + 76, rValues.fog.color, 3);
            pC[79] = rSetup.fog_type;
        }
        pC[80] = rValues.alpha_ref[0] / 255.0f;
        pC[81] = rValues.alpha_ref[1] / 255.0f;
    }

    // The current material ends: the draws that follow have none (the host draws nothing without one) until the next.
    void endCurrent() {
        static NebulaNativeMaterial sNone;
        J3DNativeState::sendMaterial(&sNone);
        sCurrentKind = CURRENT_NONE;
    }


    // Each material the game sends (NativeShader::send, this file's sends; NULL a drawing-state one).
    // The shader of the material watchMaterial last saw (J3DNativeMaterial::currentShader).
    u32 sWatchedShader = 0;

    void watchMaterial(const NebulaNativeMaterial* pNative) {
        sWatchedShader = pNative != NULL ? pNative->shader : 0;
        sCurrentKind = pNative == NULL || pNative->shader == NEBULA_SHADER_NONE ? CURRENT_NONE
                       : pNative == &sCurrent                                   ? CURRENT_J3D
                                                                                : CURRENT_SITE;
    }

    // A material with no shader in the set:
    // not drawn. Each once in the log.
    void notDrawn(u32 shader, u64 key) {
        static u64 sLogged[256];
        static u32 sLoggedCount = 0;
        bool seen = false;
        for (u32 k = 0; k < sLoggedCount; k++) {
            seen = seen || sLogged[k] == key;
        }
        if (!seen && sLoggedCount < 256) {
            sLogged[sLoggedCount++] = key;
            nebula_log("J3DNativeMaterial: material %016llx (shader %u) has no native program; not drawn\n",
                       static_cast< unsigned long long >(key), static_cast< u32 >(shader));
        }
        endCurrent();
        watchMaterial(NULL);
    }

    u32 layoutOf(u32 shader) {
        const NebulaHostServices* pHost = nebula_host();
        return pHost->material_layout(pHost->user, shader);
    }

    // The material's constants from its values, by its shader's layout.
    void packConstants(u32 shader) {
        sLayout = layoutOf(shader);
        f32* pOut = sCurrent.constants;
        for (u32 k = 0; k < cSlotCount; k++) {
            if ((sLayout & (1u << k)) != 0) {
                memcpy(pOut, sValues + cSlotAt[k], 4 * sizeof(f32));
                pOut += 4;
            }
        }
    }

    // Slot `slot` of the current material's values into its constants, if its shader reads it.
    void packSlot(u32 slot) {
        if ((sLayout & (1u << slot)) != 0) {
            memcpy(sCurrent.constants + 4 * __builtin_popcount(sLayout & ((1u << slot) - 1u)), sValues + cSlotAt[slot], 4 * sizeof(f32));
        }
    }

    // The draw's native material from the list's program and values (j3d_material.hlsli's constants).
    void sendNative(const J3DMaterialSetup& rSetup, const J3DMaterialValues& rValues, u32 shader,
                    bool keepDepthCull = false, const NebulaCookedMaterial* cooked = NULL, bool own = false) {
        // Texture matrices into their places (as the material's data has them).
        for (u32 i = 0; i < 8; i++) {
            const u8 id = rSetup.texture_matrix[i].id;
            if (!rSetup.texture_matrix[i].present || (!own && !rSetup.texture_matrix[i].listed)) {
                continue;
            }
            const f32(*pMtx)[4] = reinterpret_cast< const f32(*)[4] >(rValues.texture_matrix[i]);
            if (id >= 64) {
                J3DNativeShape::setPostTexture((id - 64) / 3, pMtx);
            } else if (id >= 30) {
                J3DNativeShape::setTexture((id - 30) / 3, pMtx, rSetup.texture_matrix[i].rows == 2 ? 2 : 3);
            }
        }
        NebulaNativeMaterial& material = sCurrent;
        memset(&material, 0, sizeof(material));
        material.shader = shader;
        material.blend_op = rSetup.blend.type;  // NEBULA_BLEND_*
        material.blend_src = rSetup.blend.source;  // NEBULA_FACTOR_*
        material.blend_dst = rSetup.blend.destination;
        material.depth_test = rSetup.z_mode.enable;
        material.depth_func = rSetup.z_mode.func;
        material.depth_write = rSetup.z_mode.update;
        material.early_depth = rSetup.z_compare_location;
        material.cull = rSetup.cull == NEBULA_CULL_ALL ? NEBULA_CULL_NONE : rSetup.cull;  // NEBULA_CULL_*
        material.color_write = 2;
        material.alpha_write = 2;
        if (keepDepthCull) {
            material.depth_test = NEBULA_NATIVE_KEPT;
            material.cull = NEBULA_NATIVE_KEPT;
        }
        if (own) {
            // A model's own copy changes values, not how it draws: a site's state changes since stay.
            material.cull = NEBULA_NATIVE_KEPT;
            material.blend_op = NEBULA_NATIVE_KEPT;
            material.depth_test = NEBULA_NATIVE_KEPT;
        }
        memset(sValues, 0, sizeof(sValues));
        fillConstants(sValues, rSetup, rValues);
        // Each coordinate's scale and each map's size (j3d_material.hlsli tex_scale): a sample reads coordinate c
        // of map m at c's scale over m's size.
        for (u32 k = 0; k < 8; k++) {
            f32* pScale = sValues + 84 + k * 4;
            const NebulaMaterialTexture& rTex = rValues.texture[k];
            pScale[2] = rTex.width != 0 ? rTex.width : 1.0f;
            pScale[3] = rTex.height != 0 ? rTex.height : 1.0f;
            pScale[0] = rValues.tex_coord_scale[k].scale_s != 0 ? rValues.tex_coord_scale[k].scale_s : pScale[2];
            pScale[1] = rValues.tex_coord_scale[k].scale_t != 0 ? rValues.tex_coord_scale[k].scale_t : pScale[3];
        }
        packConstants(shader);
        for (u32 map = 0; map < 8; map++) {
            const NebulaMaterialTexture& rTex = rValues.texture[map];
            if (rTex.image == 0) {
                continue;
            }
            material.textures[map].image = rTex.image;
            material.textures[map].repeat_s = rTex.wrap_s;
            material.textures[map].repeat_t = rTex.wrap_t;
            material.textures[map].mag_linear = rTex.mag_linear;
            material.textures[map].min_linear = rTex.min_linear;
            material.textures[map].mip_filter = rTex.mip_filter;
            material.textures[map].anisotropy = rTex.anisotropy;
            material.textures[map].edge_lod = rTex.edge_lod;
            material.textures[map].lod_bias = rTex.lod_bias;
            material.textures[map].min_lod = rTex.min_lod;
            material.textures[map].max_lod = rTex.max_lod;
        }
        // Its texgens' post-transform rows (a shape's own texgens replace them at its draw). A model's own copy leaves
        // them as the draw has them (a shape's own, ShapePacketUserData's environment maps).
        const bool keepTexGens = own;
        for (u32 i = 0; i < 8; i++) {
            material.texgen_post[i] = keepTexGens ? static_cast< u8 >(NEBULA_NATIVE_KEPT)
                                      : i < rSetup.tex_gens
                                          ? static_cast< u8 >(rSetup.tex_gen_post[i] & 0x3F)
                                          : 61;
        }
        if (!own && (rSetup.loads & J3D_LOADS_LIGHTS) != 0) {
            material.light_mask = rValues.light_mask;
            memcpy(material.light, rValues.light, sizeof(material.light));
        }
        J3DNativeShape::flushState();
        J3DNativeState::sendMaterial(&material);
        watchMaterial(&material);
        sCurrentCooked = cooked;
        sCurrentPass = 0;
    }
}  // namespace

namespace {
    u64 keyOf(const void* p) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(p));
    }

    void copyColor(u8* pOut, const NebulaColor8& rColor) {
        pOut[0] = rColor.r;
        pOut[1] = rColor.g;
        pOut[2] = rColor.b;
        pOut[3] = rColor.a;
    }

    // Map `map`'s texture (the image and palette inside the texture's resource).
    void texture(J3DTexture* pTexture, u32 map, u16 texNo, J3DMaterialValues* pOut) {
        ResTIMG* pImg = pTexture->getResTIMG(texNo);
        NebulaMaterialTexture& rOut = pOut->texture[map];
        rOut.image = keyOf(pImg->image());
        rOut.width = pImg->mWidth;
        rOut.height = pImg->mHeight;
        rOut.wrap_s = pImg->mWrapS;
        rOut.wrap_t = pImg->mWrapT;
        // u8: near 0, linear 1, then by level near/linear: 2-3 the nearest level, 4-5 between levels.
        rOut.min_linear = pImg->mMinType == 1 || pImg->mMinType == 3 || pImg->mMinType == 5;
        rOut.mag_linear = pImg->mMagType != 0;
        rOut.mip_filter = pImg->mMinType < 2 ? 0 : pImg->mMinType < 4 ? 1 : 2;
        rOut.edge_lod = pImg->mDoEdgeLod;
        rOut.bias_clamp = pImg->mBiasClamp;
        rOut.anisotropy = pImg->mMaxAnisotropy;
        rOut.min_lod = static_cast< s8 >(pImg->mMinLod) * 0.125f;
        rOut.max_lod = static_cast< s8 >(pImg->mMaxLod) * 0.125f;
        rOut.lod_bias = pImg->mLodBias * 0.01f;
    }

    void build(J3DMaterial* pMaterial, J3DMaterialSetup* pOut, J3DMaterialValues* pVal) {
        memset(pOut, 0, sizeof(*pOut));
        memset(pVal, 0, sizeof(*pVal));
        pOut->tex_gens = static_cast< u8 >(pMaterial->mTexGenNum);
        pOut->cull = pMaterial->mCull;
        for (u32 map = 0; map < 8; map++) {
            const u16 texNo = pMaterial->mTexNo[map];
            if (texNo != 0xFFFF) {
                texture(pMaterial->mTexture, map, texNo, pVal);
            }
        }
        const NebulaCookedMaterial* pCooked = pMaterial->mNativeMaterial;
        for (u32 k = 0; pCooked != NULL && k < 8; k++) {
            const u8 map = pCooked->coord_map[k];
            if (map != 255) {
                pVal->tex_coord_scale[k].scale_s = pVal->texture[map & 7].width;
                pVal->tex_coord_scale[k].scale_t = pVal->texture[map & 7].height;
            }
        }
        if (pMaterial->mHasStageColors) {
            pOut->loads |= J3D_LOADS_REGISTER_COLOR | J3D_LOADS_KONST_COLOR;
            for (u32 i = 0; i < 3; i++) {
                const J3DColorWide& rColor = pMaterial->mRegisterColor[i];
                pVal->register_color[i][0] = rColor.r;
                pVal->register_color[i][1] = rColor.g;
                pVal->register_color[i][2] = rColor.b;
                pVal->register_color[i][3] = rColor.a;
            }
            for (u32 i = 0; i < 4; i++) {
                copyColor(pVal->konst_color[i], pMaterial->mConstantColor[i]);
            }
        }
        if (pMaterial->mHasIndirect) {
            pOut->loads |= J3D_LOADS_INDIRECT;
        }
        for (u32 i = 0; i < pMaterial->mIndTexStageNum && i < 3; i++) {
            const J3DIndTexMtx& rMtx = pMaterial->mIndTexMtx[i];
            memcpy(pVal->ind_matrix[i].m, rMtx.field_0x0, sizeof(pVal->ind_matrix[i].m));
            pVal->ind_matrix[i].scale = static_cast< s8 >(rMtx.field_0x18);
        }
        // The post-transform row each coordinate uses (identity unless the model asks for post matrices).
        for (u32 i = 0; i < pOut->tex_gens && i < 8; i++) {
            pOut->tex_gen_post[i] = pMaterial->mPostTexMtx && pMaterial->mTexCoord[i].getTexGenMtx() != 60 ? static_cast< u8 >(i * 3) : 61;
        }
        pOut->loads |= J3D_LOADS_CHANNELS;
        for (u32 i = 0; i < 8; i++) {
            J3DTexMtx* pMtx = pMaterial->getTexMtx(i);
            if (pMtx == NULL) {
                continue;
            }
            pOut->texture_matrix[i].present = 1;
            pOut->texture_matrix[i].listed = i < pOut->tex_gens && pMaterial->mTexCoord[i].getTexGenMtx() != J3D_IDENTITY;
            pOut->texture_matrix[i].id = static_cast< u8 >(pMaterial->mPostTexMtx ? i * 3 + 0x40 : i * 3 + 30);
            pOut->texture_matrix[i].rows = pMaterial->mPostTexMtx || pMtx->getTexMtxInfo().mProjection != J3D_MTX2x4 ? 3 : 2;
            memcpy(pVal->texture_matrix[i], pMtx->getMtx(), sizeof(pVal->texture_matrix[i]));
        }
        for (u32 i = 0; i < 2; i++) {
            copyColor(pVal->material_color[i], pMaterial->mMatColor[i]);
            if (pMaterial->mHasAmbient) {
                pOut->loads |= J3D_LOADS_AMBIENT;
                copyColor(pVal->ambient_color[i], pMaterial->mAmbColor[i]);
            }
        }
        if (pMaterial->mHasFog) {
            pOut->loads |= J3D_LOADS_FOG;
            const J3DFogInfo& rFog = *pMaterial->mFog.getFogInfo();
            pOut->fog_type = rFog.mType;
            pVal->fog.start = rFog.mStartZ;
            pVal->fog.end = rFog.mEndZ;
            pVal->fog.near_z = rFog.mNearZ;
            pVal->fog.far_z = rFog.mFarZ;
            copyColor(pVal->fog.color, rFog.mColor);
        }
        pOut->alpha_compare.comp0 = pMaterial->mAlphaCompare.comp0;
        pVal->alpha_ref[0] = pMaterial->mAlphaCompare.ref0;
        pOut->alpha_compare.op = pMaterial->mAlphaCompare.op;
        pOut->alpha_compare.comp1 = pMaterial->mAlphaCompare.comp1;
        pVal->alpha_ref[1] = pMaterial->mAlphaCompare.ref1;
        pOut->blend.type = pMaterial->mBlend.type;
        pOut->blend.source = pMaterial->mBlend.source;
        pOut->blend.destination = pMaterial->mBlend.destination;
        pOut->z_mode.enable = pMaterial->mDepth.enable;
        pOut->z_mode.func = pMaterial->mDepth.func;
        pOut->z_mode.update = pMaterial->mDepth.update;
        pOut->z_compare_location = pMaterial->mEarlyDepth;
    }
}  // namespace

namespace J3DNativeMaterial {
    static u32 cookedShaderOf(const J3DMaterial* material) {
        const NebulaCookedMaterial* desc = material->mNativeMaterial;
        if (desc == NULL || desc->material_index != material->mIndex || desc->shader == 0) return 0;
        static bool reported = false;
        if (!reported) {
            reported = true;
            nebula_log("[cook-material] selecting cooked shader %u\n", desc->shader);
        }
        return NEBULA_SHADER_J3D_FIRST + desc->shader - 1;
    }

    void send(J3DMaterial* pMaterial) {
        static J3DMaterialSetup sSetup;
        static J3DMaterialValues sValues;
        J3DNativeShape::flushState();
        build(pMaterial, &sSetup, &sValues);
        const u32 shader = cookedShaderOf(pMaterial);
        if (shader != 0) {
            sendNative(sSetup, sValues, shader, false, pMaterial->mNativeMaterial);
            return;
        }
        notDrawn(shader, keyOf(pMaterial));
    }

    void sendOwn(J3DMaterial* pMaterial, const J3DMaterial* pShared) {
        if (pMaterial == pShared || pMaterial == NULL) {
            return;
        }
        static J3DMaterialSetup sSetup;
        static J3DMaterialValues sValues;
        build(pMaterial, &sSetup, &sValues);
        // The ambient colour stays as the actor's light set it.
        sSetup.loads = static_cast< u16 >(sSetup.loads & ~J3D_LOADS_AMBIENT);
        const u32 shader = cookedShaderOf(pMaterial);
        if (shader != 0) {
            sendNative(sSetup, sValues, shader, false, pMaterial->mNativeMaterial, true);
            return;
        }
        J3DNativeShape::flushState();
        notDrawn(shader, keyOf(pMaterial));
    }

    void watch(const NebulaNativeMaterial* pNative) {
        watchMaterial(pNative);
    }

    bool override(u32 mode, f32 a0, f32 a1, f32 ref) {
        if (sCurrentKind != CURRENT_J3D) {
            return false;
        }
        f32* pOverride = sValues + cSlotAt[SLOT_OVR];
        pOverride[0] = static_cast< f32 >(mode);
        pOverride[1] = a0;
        pOverride[2] = a1;
        pOverride[3] = ref;
        packSlot(SLOT_OVR);
        sCurrent.constants_only = 1;
        J3DNativeState::sendMaterial(&sCurrent);
        return true;
    }

    void overrideAlpha(f32 alpha) {
        f32* pOverride = sValues + cSlotAt[SLOT_OVR];
        if (sCurrentKind != CURRENT_J3D || pOverride[0] != 5.0f) {
            return;
        }
        pOverride[1] = alpha;
        packSlot(SLOT_OVR);
        sCurrent.constants_only = 1;
        J3DNativeState::sendMaterial(&sCurrent);
    }

    void swapImage(u32 map, const void* pImage) {
        if (sCurrentKind != CURRENT_J3D) {
            return;
        }
        sCurrent.textures[map].image = keyOf(pImage);
        sCurrent.constants_only = 0;
        J3DNativeState::sendMaterial(&sCurrent);
    }

    bool overrideCoordinate(const void* pImage, u16 width, u16 height) {
        if (sCurrentKind != CURRENT_J3D) {
            return false;
        }
        // Coordinate 0's scale as the new map's size.
        sValues[cSlotAt[SLOT_OVR_COORD]] = 1.0f;
        packSlot(SLOT_OVR_COORD);
        f32* pScale = sValues + cSlotAt[SLOT_TEX_SCALE0];
        pScale[0] = pScale[2] = width;
        pScale[1] = pScale[3] = height;
        packSlot(SLOT_TEX_SCALE0);
        memset(&sCurrent.textures[0], 0, sizeof(sCurrent.textures[0]));
        sCurrent.textures[0].image = keyOf(pImage);
        sCurrent.textures[0].repeat_s = 1;
        sCurrent.textures[0].repeat_t = 1;
        sCurrent.textures[0].mag_linear = 1;
        sCurrent.textures[0].min_linear = 1;
        sCurrent.constants_only = 0;
        J3DNativeState::sendMaterial(&sCurrent);
        return true;
    }

    void sendWhole(u32 shader, const J3DMaterialSetup& rSetup, const J3DMaterialValues& rValues) {
        if (shader != 0) {
            sendNative(rSetup, rValues, shader, true);
            return;
        }
        J3DNativeShape::flushState();
        notDrawn(shader, keyOf(&rSetup));
    }

    void applyPass(u8 pass) {
        if (sCurrentKind != CURRENT_J3D || sCurrentCooked == NULL) {
            notDrawn(0, pass);
            return;
        }
        sCurrentPass |= pass;
        u32 id = sCurrentCooked->passes[sCurrentPass];
        // A model a mod made for one object used by another: drawn as it is, not lost.
        if (id == 0 && (pass & NEBULA_PASS_FLEXIBLE_COORDINATES) != 0) {
            id = sCurrentCooked->passes[sCurrentPass & ~NEBULA_PASS_FLEXIBLE_COORDINATES];
        }
        if (id == 0) {
            nebula_log("cooked material %u has no pass %u\n", sCurrentCooked->material_index, sCurrentPass);
            notDrawn(0, keyOf(sCurrentCooked));
            return;
        }
        sCurrent.shader = NEBULA_SHADER_J3D_FIRST + id - 1;
        if ((pass & (NEBULA_PASS_PLAYER_OPACITY | NEBULA_PASS_LIGHT_MASK | NEBULA_PASS_SHADOW_VOLUME)) != 0) {
            sValues[80] = sValues[81] = 0;
        }
        memset(sCurrent.constants, 0, sizeof(sCurrent.constants));
        packConstants(sCurrent.shader);
        sCurrent.constants_only = 0;
        J3DNativeState::sendMaterial(&sCurrent);
        watchMaterial(&sCurrent);
    }

    void setLightAmbient(u32 channel, NebulaColor8 color) {
        setColor8(sAmbient[channel & 1], color);
        // An actor's light outranks a material's own: the material the draws use now takes it.
        if (sCurrentKind == CURRENT_J3D) {
            setColor8(sValues + 40 + (channel & 1) * 4, color);
            packSlot(SLOT_AMB_COLOR0 + (channel & 1));
            sCurrent.constants_only = 1;
            J3DNativeState::sendMaterial(&sCurrent);
        }
    }

    const f32* lightAmbient(u32 channel) {
        return sAmbient[channel & 1];
    }

    void setCurrentRegister(u32 index, NebulaColor8 color) {
        if (sCurrentKind == CURRENT_J3D) {
            setColor8(sValues + (index & 3) * 4, color);
            if ((index & 3) != 0) {
                packSlot(SLOT_REGISTER1 + (index & 3) - 1);
            }
            sCurrent.constants_only = 1;
            J3DNativeState::sendMaterial(&sCurrent);
        }
    }

    void setCurrentMaterialColor(u32 channel, NebulaColor8 color) {
        if (sCurrentKind == CURRENT_J3D) {
            setColor8(sValues + 32 + (channel & 1) * 4, color);
            packSlot(SLOT_MAT_COLOR0 + (channel & 1));
            sCurrent.constants_only = 1;
            J3DNativeState::sendMaterial(&sCurrent);
        }
    }

    void setCurrentAmbient(u32 channel, NebulaColor8 color) {
        if (sCurrentKind == CURRENT_J3D) {
            setColor8(sValues + 40 + (channel & 1) * 4, color);
            packSlot(SLOT_AMB_COLOR0 + (channel & 1));
            sCurrent.constants_only = 1;
            J3DNativeState::sendMaterial(&sCurrent);
        }
    }

    const f32* currentRegister(u32 index) {
        return sValues + (index & 3) * 4;
    }

    void setCurrentFog(f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
        if (sCurrentKind == CURRENT_J3D) {
            f32* const pC = sValues;
            pC[72] = start;
            pC[73] = end;
            pC[74] = nearZ;
            pC[75] = farZ;
            pC[76] = color.r / 255.0f;
            pC[77] = color.g / 255.0f;
            pC[78] = color.b / 255.0f;
            pC[79] = 2.0f;  // j3d_material.hlsli j3d_fog: linear
            packSlot(SLOT_FOG);
            packSlot(SLOT_FOG_COLOR);
            sCurrent.constants_only = 1;
            J3DNativeState::sendMaterial(&sCurrent);
        }
    }
};  // namespace J3DNativeMaterial
