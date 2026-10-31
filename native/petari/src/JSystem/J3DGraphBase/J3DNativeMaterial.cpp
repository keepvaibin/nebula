#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <nebula_game_api.h>
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);
extern "C" void nebula_material_watch(void (*watch)(const NebulaNativeMaterial* native));
extern "C" void nebula_log(const char* format, ...);

// A display list's material: its program and its values (nebula_game_api.h).
struct NebulaMaterialParts {
    NebulaMaterialProgram program;
    NebulaMaterialValues values;
    u32 nativeShader;  // its J3D material's NebulaShaderId (0: the GX path)
};

namespace {
    // The J3D material set (tools/dev/gen_j3d_materials.py): a program's FNV-1a hash -> its material (1-based).
    struct J3DShaderEntry {
        u64 hash;
        u32 material;
    };
    const J3DShaderEntry sJ3DShaders[] = {
#include "J3DMaterialShaders.inc"
        {0xFFFFFFFFFFFFFFFFull, 0u},
    };

    u32 nativeShaderOf(const NebulaMaterialProgram& rProgram) {
        // The program's shader-relevant bytes (tools/dev/gen_j3d_materials.py LOOKUP_ZERO): without its culling,
        // dither, early depth, origin, differ flags, fog range adjustment, blend, depth or texture matrix places.
        NebulaMaterialProgram key = rProgram;
        key.cull = key.dither = key.z_compare_location = key.origin = 0;
        key.diff_flags = 0;
        u8* pFog = reinterpret_cast< u8* >(&key.fog);
        memset(pFog + 1, 0, sizeof(key.fog) - 1);
        memset(&key.blend, 0, sizeof(key.blend));
        memset(&key.z_mode, 0, sizeof(key.z_mode));
        memset(key.texture_matrix, 0, sizeof(key.texture_matrix));
        const u8* pBytes = reinterpret_cast< const u8* >(&key);
        u64 hash = 0xCBF29CE484222325ull;
        for (u32 i = 0; i < sizeof(key); i++) {
            hash = (hash ^ pBytes[i]) * 0x100000001B3ull;
        }
        u32 lo = 0;
        u32 hi = sizeof(sJ3DShaders) / sizeof(sJ3DShaders[0]) - 1;
        while (lo < hi) {
            const u32 mid = (lo + hi) / 2;
            if (sJ3DShaders[mid].hash < hash) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return sJ3DShaders[lo].hash == hash ? NEBULA_SHADER_J3D_FIRST + sJ3DShaders[lo].material - 1 : 0;
    }

    // The register file as the J3D materials' constants lay it out (j3d_material.hlsli MaterialConstants): what the
    // J3D materials and the game's state changes set last. A native J3D material takes the parts its program does not
    // load from here (the TEV register PREV always: J3D loads registers 1-3), as GX kept them.
    f32 sRegisters[84];

    // The native material the draws use now (J3D: sCurrent, its program and values; or a site's own), and the state
    // changes made since it was sent (replayed should the GX path take over its draws).
    enum CurrentKind {
        CURRENT_NONE,
        CURRENT_J3D,
        CURRENT_SITE,
    };
    CurrentKind sCurrentKind = CURRENT_NONE;
    NebulaNativeMaterial sCurrent;
    const NebulaMaterialProgram* sCurrentProgram = NULL;
    const NebulaMaterialValues* sCurrentValues = NULL;
    NebulaStateOp sSince[64];
    u32 sSinceCount = 0;
    bool sSinceLost = false;
    bool sResending = false;

    void setColor(f32* pOut, const u8* pColor, u32 count) {
        for (u32 c = 0; c < count; c++) {
            pOut[c] = pColor[c] / 255.0f;
        }
    }

    // The parts `rProgram` loads (NEBULA_MATERIAL_LOADS_*), into the register file.
    void loadRegisters(const NebulaMaterialProgram& rProgram, const NebulaMaterialValues& rValues) {
        f32* pC = sRegisters;
        const u16 loads = rProgram.loads;
        if ((loads & NEBULA_MATERIAL_LOADS_TEV_COLOR) != 0) {
            for (u32 r = 0; r < 3; r++) {
                for (u32 c = 0; c < 4; c++) {
                    pC[4 + r * 4 + c] = rValues.tev_color[r][c] / 255.0f;
                }
            }
        }
        if ((loads & NEBULA_MATERIAL_LOADS_KONST_COLOR) != 0) {
            for (u32 k = 0; k < 4; k++) {
                setColor(pC + 16 + k * 4, rValues.konst_color[k], 4);
            }
        }
        for (u32 ch = 0; ch < 2; ch++) {
            if ((loads & NEBULA_MATERIAL_LOADS_CHANNELS) != 0) {
                setColor(pC + 32 + ch * 4, rValues.material_color[ch], 4);
            }
            if ((loads & NEBULA_MATERIAL_LOADS_AMBIENT) != 0) {
                setColor(pC + 40 + ch * 4, rValues.ambient_color[ch], 4);
            }
        }
        if ((loads & NEBULA_MATERIAL_LOADS_INDIRECT) != 0) {
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
                        pC[48 + (m * 2 + row) * 4 + c] = rValues.ind_matrix[m].m[row][c] * scale;
                    }
                }
            }
        }
        if ((loads & NEBULA_MATERIAL_LOADS_FOG) != 0) {
            pC[72] = rValues.fog.start;
            pC[73] = rValues.fog.end;
            pC[74] = rValues.fog.near_z;
            pC[75] = rValues.fog.far_z;
            setColor(pC + 76, rValues.fog.color, 3);
        }
        pC[80] = rValues.alpha_ref[0] / 255.0f;
        pC[81] = rValues.alpha_ref[1] / 255.0f;
    }

    // A state change of a register the J3D constants hold, into the register file: whether it was one.
    bool setRegister(const NebulaStateOp& rOp) {
        f32* pC = sRegisters;
        const u8* a = rOp.a;
        switch (rOp.kind) {
        case NEBULA_STATE_TEV_COLOR:
            setColor(pC + (a[0] & 3) * 4, a + 1, 4);
            return true;
        case NEBULA_STATE_TEV_COLOR_S10: {
            f32* pReg = pC + (a[0] & 3) * 4;
            pReg[0] = static_cast< s16 >(rOp.size[0]) / 255.0f;
            pReg[1] = static_cast< s16 >(rOp.size[1]) / 255.0f;
            pReg[2] = rOp.f[0] / 255.0f;
            pReg[3] = rOp.f[1] / 255.0f;
            return true;
        }
        case NEBULA_STATE_TEV_KCOLOR:
            setColor(pC + 16 + (a[0] & 3) * 4, a + 1, 4);
            return true;
        case NEBULA_STATE_MATERIAL_COLOR:
        case NEBULA_STATE_AMBIENT_COLOR: {
            // GXSetChan*Color: a colour channel sets r, g, b, an alpha channel a; GDSetChan*Color the whole colour.
            f32* pColor = pC + (rOp.kind == NEBULA_STATE_MATERIAL_COLOR ? 32 : 40) + (a[0] & 1) * 4;
            const bool partial = a[5] != 0 && a[0] < 4;
            for (u32 c = 0; c < 4; c++) {
                if (!partial || (a[0] < 2) == (c < 3)) {
                    pColor[c] = a[1 + c] / 255.0f;
                }
            }
            return true;
        }
        case NEBULA_STATE_FOG:
            pC[72] = rOp.f[0];
            pC[73] = rOp.f[1];
            pC[74] = rOp.f[2];
            pC[75] = rOp.f[3];
            setColor(pC + 76, a + 1, 3);
            return true;
        case NEBULA_STATE_ALPHA_COMPARE:
            pC[80] = a[1] / 255.0f;
            pC[81] = a[4] / 255.0f;
            return true;
        case NEBULA_STATE_IND_TEX_MATRIX: {
            // GXSetIndTexMtx: the matrix (or a dynamic S or T matrix in its place), its terms by the scale exponent.
            const u32 id = a[0] >= 9 ? a[0] - 9 : a[0] >= 5 ? a[0] - 5 : a[0] >= 1 ? a[0] - 1 : 3;
            if (id >= 3) {
                return false;
            }
            f32 scale = 1.0f;
            for (s32 e = static_cast< s8 >(a[1]); e > 0; e--) {
                scale *= 2.0f;
            }
            for (s32 e = static_cast< s8 >(a[1]); e < 0; e++) {
                scale *= 0.5f;
            }
            for (u32 w = 0; w < 3; w++) {
                for (u32 row = 0; row < 2; row++) {
                    pC[48 + (id * 2 + row) * 4 + w] = rOp.f[2 * w + row] * scale;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }

    // Whether a state change alters what the current native material's shader computes (its stages, channels,
    // coordinates, fog type or alpha test) rather than a value it takes or its fixed-function state.
    bool changesProgram(const NebulaStateOp& rOp) {
        switch (rOp.kind) {
        case NEBULA_STATE_GEN_MODE:
        case NEBULA_STATE_GEN_MODE2:
        case NEBULA_STATE_NUM_TEX_GENS:
        case NEBULA_STATE_NUM_CHANS:
        case NEBULA_STATE_NUM_TEV_STAGES:
        case NEBULA_STATE_NUM_IND_STAGES:
        case NEBULA_STATE_CHANNEL:
        case NEBULA_STATE_TEV_COLOR_CALC:
        case NEBULA_STATE_TEV_ALPHA_CALC:
        case NEBULA_STATE_TEV_DIRECT:
        case NEBULA_STATE_TEV_INDIRECT:
        case NEBULA_STATE_TEV_ORDER:
        case NEBULA_STATE_TEV_COLOR_IN:
        case NEBULA_STATE_TEV_ALPHA_IN:
        case NEBULA_STATE_TEV_COLOR_OP:
        case NEBULA_STATE_TEV_ALPHA_OP:
        case NEBULA_STATE_TEV_KCOLOR_SEL:
        case NEBULA_STATE_TEV_KALPHA_SEL:
        case NEBULA_STATE_TEV_SWAP_MODE:
        case NEBULA_STATE_TEV_SWAP_TABLE:
        case NEBULA_STATE_TEV_STAGE_ORDER:
        case NEBULA_STATE_TEX_COORD_GEN:
        case NEBULA_STATE_IND_TEX_SCALE:
        case NEBULA_STATE_IND_TEX_ORDER:
        case NEBULA_STATE_Z_TEXTURE:
            return true;
        case NEBULA_STATE_FOG:
            return sCurrentKind == CURRENT_J3D && rOp.a[0] != sCurrentProgram->fog.type;
        case NEBULA_STATE_ALPHA_COMPARE:
            return sCurrentKind == CURRENT_J3D &&
                   (rOp.a[0] != sCurrentProgram->alpha_compare.comp0 || rOp.a[2] != sCurrentProgram->alpha_compare.op ||
                    rOp.a[3] != sCurrentProgram->alpha_compare.comp1);
        default:
            return false;
        }
    }

    // The draws go back to the drawing state: a J3D material's own GX program and values, then the state changes made
    // since it was sent (as GX had them); a site's material just ends.
    void endCurrent() {
        const NebulaHostServices* pHost = nebula_host();
        if (sCurrentKind == CURRENT_J3D) {
            const u64 key = static_cast< u64 >(reinterpret_cast< uintptr_t >(sCurrentProgram));
            pHost->material_program(pHost->user, key, sCurrentProgram);
            const u32 since = sSinceCount;
            pHost->material(pHost->user, key, sCurrentValues);
            if (sSinceLost) {
                nebula_log("J3DNativeMaterial: more than %u state changes since a native material; the oldest are lost\n",
                         static_cast< u32 >(sizeof(sSince) / sizeof(sSince[0])));
            }
            if (since != 0) {
                pHost->state(pHost->user, sSince, since);
            }
        } else {
            static NebulaNativeMaterial sNone;
            pHost->native_material(pHost->user, &sNone);
        }
        sCurrentKind = CURRENT_NONE;
    }

    // Each material the game sends (sdk/core.c nebula_material_watch).
    void watchMaterial(const NebulaNativeMaterial* pNative) {
        if (sResending) {
            return;
        }
        sCurrentKind = pNative == NULL || pNative->shader == NEBULA_SHADER_NONE ? CURRENT_NONE
                       : pNative == &sCurrent                                   ? CURRENT_J3D
                                                                                : CURRENT_SITE;
        sSinceCount = 0;
        sSinceLost = false;
    }

    struct MaterialWatch {
        MaterialWatch() { nebula_material_watch(watchMaterial); }
    };
    MaterialWatch sMaterialWatch;

    u8 nativeFactor(u8 gx, bool source) {
        // GX's colour factors name the other side's colour (a source factor 2 is the destination's colour).
        if (gx == 2 || gx == 3) {
            return source ? static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_DST_COLOR : NEBULA_FACTOR_INV_DST_COLOR)
                          : static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_SRC_COLOR : NEBULA_FACTOR_INV_SRC_COLOR);
        }
        return gx;
    }

    // The draw's native material from the list's program and values (j3d_material.hlsli's constants).
    void sendNative(const NebulaMaterialProgram& rProgram, const NebulaMaterialValues& rValues, u32 shader) {
        // Texture matrices into their places (as the material's display list loads them).
        const bool differed = rProgram.diff_flags != 0;
        if (!differed || ((rProgram.diff_flags >> 8) & 15) != 0) {
            for (u32 i = 0; i < 8; i++) {
                const u8 id = rProgram.texture_matrix[i].id;
                if (!rProgram.texture_matrix[i].present || (!differed && !rProgram.texture_matrix[i].listed)) {
                    continue;
                }
                const f32(*pMtx)[4] = reinterpret_cast< const f32(*)[4] >(rValues.texture_matrix[i]);
                if (id >= 64) {
                    J3DNativeShape::setPostTexture((id - 64) / 3, pMtx);
                } else if (id >= 30) {
                    J3DNativeShape::setTexture((id - 30) / 3, pMtx, rProgram.texture_matrix[i].rows == 2 ? 2 : 3);
                }
            }
        }
        NebulaNativeMaterial& material = sCurrent;
        memset(&material, 0, sizeof(material));
        material.shader = shader;
        const u8 type = rProgram.blend.type;
        material.blend_op = type == GX_BM_BLEND ? NEBULA_BLEND_ADD
                            : type == GX_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
                            : type == GX_BM_LOGIC ? NEBULA_BLEND_LOGIC
                                                  : NEBULA_BLEND_REPLACE;
        material.blend_src = nativeFactor(rProgram.blend.source, true);
        material.blend_dst = nativeFactor(rProgram.blend.destination, false);
        material.logic_op = rProgram.blend.logic_op;
        material.depth_test = rProgram.z_mode.enable;
        material.depth_func = rProgram.z_mode.func;
        material.depth_write = rProgram.z_mode.update;
        material.early_depth = rProgram.z_compare_location;
        material.cull = rProgram.cull == GX_CULL_BACK ? NEBULA_CULL_BACK
                        : rProgram.cull == GX_CULL_FRONT ? NEBULA_CULL_FRONT
                                                         : NEBULA_CULL_NONE;
        material.color_write = 2;
        material.alpha_write = 2;
        loadRegisters(rProgram, rValues);
        memcpy(material.constants, sRegisters, sizeof(sRegisters));
        for (u32 map = 0; map < 8; map++) {
            const NebulaMaterialTexture& rTex = rValues.texture[map];
            if (rTex.image == 0) {
                continue;
            }
            material.textures[map].image = rTex.image;
            material.textures[map].width = rTex.width;
            material.textures[map].height = rTex.height;
            material.textures[map].format = rTex.format;
            material.textures[map].repeat_s = rTex.wrap_s;
            material.textures[map].repeat_t = rTex.wrap_t;
            material.textures[map].mag_linear = rTex.mag_filter != 0 ? 1 : 0;
            material.textures[map].min_linear = rTex.min_filter == 1 || rTex.min_filter == 3 || rTex.min_filter == 5;
            material.textures[map].mip_filter = rTex.min_filter < 2 ? 0 : rTex.min_filter < 4 ? 1 : 2;
            material.textures[map].anisotropy = rTex.anisotropy;
            material.textures[map].edge_lod = rTex.edge_lod;
            material.textures[map].lod_bias = rTex.lod_bias;
            material.textures[map].min_lod = rTex.min_lod;
            material.textures[map].max_lod = rTex.max_lod;
        }
        // The lights the material's colour block loads (J3DColorBlockLightOn), as its list loaded them.
        if ((rProgram.loads & NEBULA_MATERIAL_LOADS_LIGHTS) != 0) {
            material.light_mask = rValues.light_mask;
            memcpy(material.light, rValues.light, sizeof(material.light));
        }
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, &material);
        sCurrentProgram = &rProgram;
        sCurrentValues = &rValues;
    }
}  // namespace

namespace {
    u64 keyOf(const void* p) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(p));
    }

    void copyColor(u8* pOut, const GXColor& rColor) {
        pOut[0] = rColor.r;
        pOut[1] = rColor.g;
        pOut[2] = rColor.b;
        pOut[3] = rColor.a;
    }

    // Map `map`'s texture as J3D's loadTexNo sets it up (the image and palette inside the texture's resource).
    void texture(J3DTexture* pTexture, u32 map, u16 texNo, NebulaMaterialValues* pOut, u16* pSizes) {
        ResTIMG* pImg = pTexture->getResTIMG(texNo);
        NebulaMaterialTexture& rOut = pOut->texture[map];
        rOut.image = keyOf(reinterpret_cast< u8* >(pImg) + pImg->mImageDataOffset);
        rOut.width = pImg->mWidth;
        rOut.height = pImg->mHeight;
        rOut.format = pImg->mFormat & 0x0F;
        rOut.wrap_s = pImg->mWrapS;
        rOut.wrap_t = pImg->mWrapT;
        rOut.min_filter = pImg->mMinType;
        rOut.mag_filter = pImg->mMagType;
        rOut.mipmaps = pImg->mImageNum;
        rOut.edge_lod = pImg->mDoEdgeLod;
        rOut.bias_clamp = pImg->mBiasClamp;
        rOut.anisotropy = pImg->mMaxAnisotropy;
        rOut.min_lod = static_cast< s8 >(pImg->mMinLod) * 0.125f;
        rOut.max_lod = static_cast< s8 >(pImg->mMaxLod) * 0.125f;
        rOut.lod_bias = pImg->mLodBias * 0.01f;
        if (pImg->mPaletteName == true) {
            rOut.palette = keyOf(reinterpret_cast< u8* >(pImg) + pImg->mPaletteDataOffset);
            rOut.palette_entries = pImg->mPaletteNum;
            rOut.palette_format = pImg->mPaletteFormat;
        }
        // The texture coordinate scale J3D keeps per map (J3DSys::sTexCoordScaleTable): the image's size.
        pSizes[map * 2] = pImg->mWidth;
        pSizes[map * 2 + 1] = pImg->mHeight;
    }

    // A texture coordinate's scale entry as loadTexCoordScale writes it from its map's table entry.
    void coordScale(NebulaMaterialValues* pOut, u32 coord, u32 map) {
        const J3DTexCoordScaleInfo& rInfo = J3DSys::sTexCoordScaleTable[map & 7];
        pOut->tex_coord_scale[coord & 7].scale_s = rInfo.field_0x00;
        pOut->tex_coord_scale[coord & 7].scale_t = rInfo.field_0x02;
        pOut->tex_coord_scale[coord & 7].bias_s = rInfo.field_0x04 == 1;
        pOut->tex_coord_scale[coord & 7].bias_t = rInfo.field_0x06 == 1;
    }
}  // namespace

namespace J3DNativeMaterial {
    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterialParts* pParts) {
        memset(pParts, 0, sizeof(*pParts));
        NebulaMaterialProgram* pOut = &pParts->program;
        NebulaMaterialValues* pVal = &pParts->values;
        J3DTevBlock* pTev = pMaterial->mTevBlock;
        J3DIndBlock* pInd = pMaterial->mIndBlock;
        J3DTexGenBlock* pTexGen = pMaterial->mTexGenBlock;
        J3DColorBlock* pColor = pMaterial->mColorBlock;
        J3DPEBlock* pPE = pMaterial->mPEBlock;

        pOut->tev_stages = pTev->getTevStageNum();
        pOut->tex_gens = static_cast< u8 >(pTexGen->getTexGenNum());
        pOut->channels = pColor->getColorChanNum();
        pOut->ind_stages = pInd->getIndTexStageNum();
        pOut->cull = pColor->getCullMode();

        // The null TEV block writes no textures, orders or stages (its stage count still sets the mode).
        const bool tev = pTev->getTevOrder(0) != NULL;
        if (tev) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_TEV;
        }
        // Textures first: their sizes fill the coordinate scale table the stages then read.
        u16 sizes[16];
        memset(sizes, 0, sizeof(sizes));
        for (u32 map = 0; tev && map < 8; map++) {
            const u16 texNo = pTev->getTexNo(map);
            if (texNo != 0xFFFF) {
                texture(pTexture, map, texNo, pVal, sizes);
                J3DSys::sTexCoordScaleTable[map].field_0x00 = sizes[map * 2];
                J3DSys::sTexCoordScaleTable[map].field_0x02 = sizes[map * 2 + 1];
            }
        }
        for (u32 i = 0; tev && i < pOut->tev_stages; i++) {
            NebulaMaterialTevStage& rOut = pOut->tev[i];
            const J3DTevOrderInfo& rOrder = pTev->getTevOrder(i)->getTevOrderInfo();
            rOut.tex_coord = rOrder.mTexCoord;
            rOut.tex_map = rOrder.mTexMap;
            rOut.channel = rOrder.mColorChan;
            coordScale(pVal, rOrder.mTexCoord, rOrder.mTexMap);
            const J3DTevStage& rStage = *pTev->getTevStage(i);
            for (u32 k = 0; k < 4; k++) {
                rOut.color_in[k] = rStage.mColorIn[k];
                rOut.alpha_in[k] = rStage.mAlphaIn[k];
            }
            rOut.color_op = rStage.mColorOp;
            rOut.color_bias = rStage.mColorBias;
            rOut.color_scale = rStage.mColorScale;
            rOut.color_clamp = rStage.mColorClamp;
            rOut.color_reg = rStage.mColorReg;
            rOut.alpha_op = rStage.mAlphaOp;
            rOut.alpha_bias = rStage.mAlphaBias;
            rOut.alpha_scale = rStage.mAlphaScale;
            rOut.alpha_clamp = rStage.mAlphaClamp;
            rOut.alpha_reg = rStage.mAlphaReg;
            rOut.ras_swap = rStage.mRasSel;
            rOut.tex_swap = rStage.mTexSel;
            const J3DIndTevStage& rInd = *pTev->getIndTevStage(i);
            rOut.ind_stage = rInd.mIndStage;
            rOut.ind_format = rInd.mIndFormat;
            rOut.ind_bias = rInd.mBiasSel;
            rOut.ind_matrix = rInd.mMtxSel;
            rOut.ind_wrap_s = rInd.mWrapS;
            rOut.ind_wrap_t = rInd.mWrapT;
            rOut.ind_prev = rInd.mPrev;
            rOut.ind_lod = rInd.mLod;
            rOut.ind_alpha = rInd.mAlphaSel;
        }
        // Orders are written in pairs: an odd count's last stage pairs with the next entry the block holds.
        if (tev && (pOut->tev_stages & 1) != 0 && pOut->tev_stages < 16 && pTev->getType() != 'TVB1') {
            const J3DTevOrderInfo& rOrder = pTev->getTevOrder(pOut->tev_stages)->getTevOrderInfo();
            pOut->tev[pOut->tev_stages].tex_coord = rOrder.mTexCoord;
            pOut->tev[pOut->tev_stages].tex_map = rOrder.mTexMap;
            pOut->tev[pOut->tev_stages].channel = rOrder.mColorChan;
            coordScale(pVal, rOrder.mTexCoord, rOrder.mTexMap);
        }
        // Konst selectors for all 16 stages, as the blocks write them: their own entries repeated (two in J3DTevBlock2,
        // four in J3DTevBlock4, sixteen in J3DTevBlock16).
        const u32 tevType = pTev->getType();
        const u32 konst = tevType == 'TV16' ? 16 : tevType == 'TVB4' ? 4 : tevType == 'TVB2' ? 2 : 0;
        for (u32 i = 0; konst != 0 && i < 16; i++) {
            pOut->tev[i].konst_color = pTev->getTevKColorSel(i % konst);
            pOut->tev[i].konst_alpha = pTev->getTevKAlphaSel(i % konst);
        }
        // J3DTevBlock1 writes its one stage's order paired with a null second stage.
        if (tevType == 'TVB1') {
            pOut->tev[1].tex_coord = GX_TEXCOORD_NULL;
            pOut->tev[1].tex_map = GX_TEXMAP_NULL;
            pOut->tev[1].channel = GX_COLOR_NULL;
        }
        // The parts a TEV block variant keeps (J3DTevBlock1 has no colours, selectors or swap tables; the patched
        // block no swap tables): left as set before.
        if (pTev->getTevColor(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_TEV_COLOR;
            for (u32 i = 0; i < 3; i++) {
                const J3DGXColorS10& rColor = *pTev->getTevColor(i);
                pVal->tev_color[i][0] = rColor.r;
                pVal->tev_color[i][1] = rColor.g;
                pVal->tev_color[i][2] = rColor.b;
                pVal->tev_color[i][3] = rColor.a;
            }
        }
        if (pTev->getTevKColor(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_KONST_COLOR;
            for (u32 i = 0; i < 4; i++) {
                copyColor(pVal->konst_color[i], *pTev->getTevKColor(i));
            }
        }
        if (konst != 0) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_KONST_SELECT | NEBULA_MATERIAL_LOADS_SWAP_TABLE;
            for (u32 i = 0; i < 4; i++) {
                const J3DTevSwapModeTable& rSwap = *pTev->getTevSwapModeTable(i);
                pOut->swap_table[i][0] = rSwap.getR();
                pOut->swap_table[i][1] = rSwap.getG();
                pOut->swap_table[i][2] = rSwap.getB();
                pOut->swap_table[i][3] = rSwap.getA();
            }
        }

        // J3DIndBlockFull writes all four orders and coordinate scales whatever its stage count (the null block nothing).
        if (pInd->getIndTexOrder(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_INDIRECT;
        }
        for (u32 i = 0; i < 4 && pInd->getIndTexOrder(0) != NULL; i++) {
            J3DIndTexOrder* pOrder = pInd->getIndTexOrder(i);
            J3DIndTexCoordScale* pScale = pInd->getIndTexCoordScale(i);
            pOut->ind_order[i].coord = pOrder->getCoord();
            pOut->ind_order[i].map = pOrder->getMap();
            pOut->ind_order[i].scale_s = pScale->getScaleS();
            pOut->ind_order[i].scale_t = pScale->getScaleT();
            coordScale(pVal, pOrder->getCoord(), pOrder->getMap());
        }
        for (u32 i = 0; i < pOut->ind_stages && i < 3; i++) {
            const J3DIndTexMtx& rMtx = *pInd->getIndTexMtx(i);
            memcpy(pVal->ind_matrix[i].m, rMtx.field_0x0, sizeof(pVal->ind_matrix[i].m));
            pVal->ind_matrix[i].scale = static_cast< s8 >(rMtx.field_0x18);
        }

        for (u32 i = 0; i < pOut->tex_gens && i < 8; i++) {
            const J3DTexCoord& rCoord = *pTexGen->getTexCoord(i);
            pOut->tex_gen[i].type = rCoord.getTexGenType();
            pOut->tex_gen[i].source = rCoord.getTexGenSrc();
            pOut->tex_gen[i].matrix = static_cast< u8 >(rCoord.getTexMtxReg());
            // The post-transform matrix loadTexCoordGens names (identity unless j3dSys asks for post matrices).
            pOut->tex_gen[i].post_matrix =
                j3dSys.checkFlag(0x40000000) && rCoord.getTexGenMtx() != 60 ? static_cast< u8 >(i * 3) : 61;
        }

        // The null colour block writes no channels or colours.
        if (pColor->getColorChan(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_CHANNELS;
        }
        // Texture matrices as the texgen block's list loads them (J3DTexMtx::load, with this moment's j3dSys flags).
        for (u32 i = 0; i < 8; i++) {
            J3DTexMtx* pMtx = pTexGen->getTexMtx(i);
            if (pMtx == NULL) {
                continue;
            }
            const bool post = j3dSys.checkFlag(J3DSysFlag_PostTexMtx);
            pOut->texture_matrix[i].present = 1;
            pOut->texture_matrix[i].listed = i < pOut->tex_gens && pTexGen->getTexCoord(i)->getTexGenMtx() != GX_IDENTITY;
            pOut->texture_matrix[i].id = static_cast< u8 >(post ? i * 3 + 0x40 : i * 3 + 30);
            pOut->texture_matrix[i].rows = post || pMtx->getTexMtxInfo().mProjection != GX_MTX2x4 ? 3 : 2;
            memcpy(pVal->texture_matrix[i], pMtx->getMtx(), sizeof(pVal->texture_matrix[i]));
        }
        for (u32 i = 0; i < 4 && pColor->getColorChan(0) != NULL; i++) {
            const J3DColorChan& rChan = *pColor->getColorChan(i);
            pOut->channel[i].enable = rChan.getEnable();
            pOut->channel[i].material_source = rChan.getMatSrc();
            pOut->channel[i].ambient_source = rChan.getAmbSrc();
            pOut->channel[i].light_mask = rChan.getLightMask();
            pOut->channel[i].diffuse = rChan.getDiffuseFn();
            pOut->channel[i].attenuation = rChan.getAttnFn();
        }
        for (u32 i = 0; i < 2; i++) {
            if (pColor->getMatColor(i) != NULL) {
                copyColor(pVal->material_color[i], *pColor->getMatColor(i));
            }
            if (pColor->getAmbColor(i) != NULL) {
                pOut->loads |= NEBULA_MATERIAL_LOADS_AMBIENT;
                copyColor(pVal->ambient_color[i], *pColor->getAmbColor(i));
            }
        }
        // Lights only from the blocks that load them (J3DColorBlockLightOn).
        for (u32 i = 0; i < 8 && pColor->getType() == 'CLON'; i++) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_LIGHTS;
            const J3DLightObj* pLight = pColor->getLight(i);
            if (pLight == NULL) {
                continue;
            }
            pVal->light_mask |= 1 << i;
            const J3DLightInfo& rInfo = pLight->mInfo;
            NebulaMaterialLight& rOut = pVal->light[i];
            rOut.position[0] = rInfo.mLightPosition.x;
            rOut.position[1] = rInfo.mLightPosition.y;
            rOut.position[2] = rInfo.mLightPosition.z;
            rOut.direction[0] = rInfo.mLightDirection.x;
            rOut.direction[1] = rInfo.mLightDirection.y;
            rOut.direction[2] = rInfo.mLightDirection.z;
            rOut.angle[0] = rInfo.mCosAtten.x;
            rOut.angle[1] = rInfo.mCosAtten.y;
            rOut.angle[2] = rInfo.mCosAtten.z;
            rOut.distance[0] = rInfo.mDistAtten.x;
            rOut.distance[1] = rInfo.mDistAtten.y;
            rOut.distance[2] = rInfo.mDistAtten.z;
            copyColor(rOut.color, rInfo.mColor);
        }

        // The preset blocks write fixed modes and no fog, and keep the dither (J3DPEBlockOpa, TexEdge, Xlu load).
        const u32 peType = pPE->getType();
        if (peType == 'PEOP' || peType == 'PEED' || peType == 'PEXL') {
            const bool edge = peType == 'PEED';
            const bool xlu = peType == 'PEXL';
            pOut->alpha_compare.comp0 = edge ? GX_GEQUAL : GX_ALWAYS;
            pVal->alpha_ref[0] = edge ? 0x80 : 0;
            pOut->alpha_compare.op = GX_AOP_AND;
            pOut->alpha_compare.comp1 = edge ? GX_LEQUAL : GX_ALWAYS;
            pVal->alpha_ref[1] = edge ? 0xFF : 0;
            pOut->blend.type = xlu ? GX_BM_BLEND : GX_BM_NONE;
            pOut->blend.source = xlu ? GX_BL_SRCALPHA : GX_BL_ONE;
            pOut->blend.destination = xlu ? GX_BL_INVSRCALPHA : GX_BL_ZERO;
            pOut->blend.logic_op = GX_LO_COPY;
            pOut->z_mode.enable = 1;
            pOut->z_mode.func = GX_LEQUAL;
            pOut->z_mode.update = xlu ? 0 : 1;
            pOut->z_compare_location = edge ? 0 : 1;
            return;
        }
        pOut->loads |= NEBULA_MATERIAL_LOADS_DITHER;
        if (J3DFog* pFog = pPE->getFog()) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_FOG;
            const J3DFogInfo& rFog = *pFog->getFogInfo();
            pOut->fog.type = rFog.mType;
            pOut->fog.adjust_enable = rFog.mAdjEnable;
            pOut->fog.adjust_center = rFog.mCenter;
            pVal->fog.start = rFog.mStartZ;
            pVal->fog.end = rFog.mEndZ;
            pVal->fog.near_z = rFog.mNearZ;
            pVal->fog.far_z = rFog.mFarZ;
            copyColor(pVal->fog.color, rFog.mColor);
            for (u32 i = 0; i < 10; i++) {
                pOut->fog.adjust[i] = rFog.mFogAdjTable.r[i];
            }
        }
        const J3DAlphaComp& rAlpha = *pPE->getAlphaComp();
        pOut->alpha_compare.comp0 = rAlpha.getComp0();
        pVal->alpha_ref[0] = rAlpha.getRef0();
        pOut->alpha_compare.op = rAlpha.getOp();
        pOut->alpha_compare.comp1 = rAlpha.getComp1();
        pVal->alpha_ref[1] = rAlpha.getRef1();
        const J3DBlend& rBlend = *pPE->getBlend();
        pOut->blend.type = rBlend.mType;
        pOut->blend.source = rBlend.mSrcFactor;
        pOut->blend.destination = rBlend.mDstFactor;
        pOut->blend.logic_op = rBlend.mOp;
        const J3DZMode& rZ = *pPE->getZMode();
        pOut->z_mode.enable = rZ.getCompareEnable();
        pOut->z_mode.func = rZ.getFunc();
        pOut->z_mode.update = rZ.getUpdateEnable();
        pOut->z_compare_location = pPE->getZCompLoc();
        pOut->dither = pPE->getDither();
    }

    NebulaMaterialParts* create() {
        return new (0x20) NebulaMaterialParts;
    }

    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList, u32 diffFlags) {
        if (pList != NULL && pList->mpNebulaMaterial != NULL) {
            NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
            static NebulaMaterialProgram sRegistered;
            sRegistered = rParts.program;
            build(pMaterial, pTexture, &rParts);
            rParts.program.diff_flags = diffFlags;
            rParts.program.origin = diffFlags != 0 ? 2 : pTexture == j3dSys.getTexture() ? 3 : 1;
            rParts.nativeShader = nativeShaderOf(rParts.program);
            // The program the host has for this list (GX path only): sent when the list's first capture makes it, or
            // when a patch changes it (an animated material's captures change only its values).
            if (rParts.nativeShader == 0 &&
                (!pList->mNebulaCaptured || memcmp(&sRegistered, &rParts.program, sizeof(sRegistered)) != 0)) {
                const NebulaHostServices* pHost = nebula_host();
                pHost->material_program(pHost->user, keyOf(pList), &rParts.program);
            }
            pList->mNebulaCaptured = true;
        }
    }

    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList) {
        static NebulaMaterialParts sParts;
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        if (pList != NULL && pList->mNebulaCaptured) {
            const NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
            if (rParts.nativeShader != 0) {
                sendNative(rParts.program, rParts.values, rParts.nativeShader);
                return;
            }
            loadRegisters(rParts.program, rParts.values);
            pHost->material(pHost->user, keyOf(pList), &rParts.values);
            return;
        }
        // A list from the model file, which has no snapshot: its material built now, its program with it.
        build(pMaterial, pTexture, &sParts);
        if (const u32 shader = nativeShaderOf(sParts.program)) {
            sendNative(sParts.program, sParts.values, shader);
            return;
        }
        loadRegisters(sParts.program, sParts.values);
        pHost->material_program(pHost->user, keyOf(&sParts), &sParts.program);
        pHost->material(pHost->user, keyOf(&sParts), &sParts.values);
    }

    void sendDiffered(J3DDisplayListObj* pList) {
        if (pList == NULL || !pList->mNebulaCaptured) {
            return;
        }
        const NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
        if (rParts.nativeShader != 0) {
            sendNative(rParts.program, rParts.values, rParts.nativeShader);
            return;
        }
        J3DNativeShape::flushState();
        loadRegisters(rParts.program, rParts.values);
        const NebulaHostServices* pHost = nebula_host();
        pHost->material(pHost->user, keyOf(pList), &rParts.values);
    }

    void state(const NebulaStateOp* pOps, u32 count) {
        bool changed = false;
        for (u32 i = 0; i < count; i++) {
            const NebulaStateOp& rOp = pOps[i];
            if (sCurrentKind != CURRENT_NONE && changesProgram(rOp)) {
                endCurrent();
            }
            changed = setRegister(rOp) || changed;
            if (sCurrentKind == CURRENT_J3D) {
                if (sSinceCount < sizeof(sSince) / sizeof(sSince[0])) {
                    sSince[sSinceCount++] = rOp;
                } else {
                    sSinceLost = true;
                }
            }
        }
        // A value the current J3D material takes: the material again with its constants (its state, textures and
        // lights as set since).
        if (changed && sCurrentKind == CURRENT_J3D) {
            memcpy(sCurrent.constants, sRegisters, sizeof(sRegisters));
            sCurrent.constants_only = 1;
            sResending = true;
            const NebulaHostServices* pHost = nebula_host();
            pHost->native_material(pHost->user, &sCurrent);
            sResending = false;
        }
    }

    void sweep(J3DModelData* pModelData) {
        static s32 sWanted = -1;
        const NebulaHostServices* pHost = nebula_host();
        if (sWanted < 0) {
            sWanted = pHost->option(pHost->user, NEBULA_OPTION_TEST_MATERIAL_SWEEP) != 0 ? 1 : 0;
        }
        if (sWanted == 0) {
            return;
        }
        static NebulaMaterialParts sParts;
        for (u16 i = 0; i < pModelData->getMaterialNum(); i++) {
            J3DMaterial* pMaterial = pModelData->getMaterialNodePointer(i);
            build(pMaterial, pModelData->getTexture(), &sParts);
            if (nativeShaderOf(sParts.program) == 0) {
                pHost->material_program(pHost->user, keyOf(pMaterial), &sParts.program);
            }
        }
    }
};  // namespace J3DNativeMaterial
