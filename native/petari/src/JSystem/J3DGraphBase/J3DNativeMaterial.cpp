#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
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
struct NebulaMaterialParts {
    J3DMaterialSetup setup;
    J3DMaterialValues values;
    u32 nativeShader;
    const NebulaCookedMaterial* cooked;
};

namespace {
    // The register file as the J3D materials' constants lay it out (j3d_material.hlsli MaterialConstants): what the
    // J3D materials and the game's state changes set last. A native J3D material takes the parts its program does not
    // load from here (the output register always: J3D loads registers 1-3).
    f32 sRegisters[84];

    // The native material the draws use now (J3D: sCurrent, its program and values; or a site's own), and the state
    // changes made since it was sent (replayed should the old path take over its draws).
    enum CurrentKind {
        CURRENT_NONE,
        CURRENT_J3D,
        CURRENT_SITE,
    };
    CurrentKind sCurrentKind = CURRENT_NONE;
    // A site's material whose constants begin with the register file (constants_only bit 1): its copy, resent with
    // the register file's values when the game changes one.
    NebulaNativeMaterial sSiteRegisters;
    bool sSiteFollows = false;
    NebulaNativeMaterial sCurrent;
    const J3DMaterialSetup* sCurrentSetup = NULL;
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

    // The parts `rSetup` loads (J3D_LOADS_*), into the register file. An instance's differed list
    // (diff_flags) loads only the parts its flags name, as J3D's list does and the drawing state takes it
    // (draw_state.cpp set_material): register and constant colours under TevReg, material colours under MatColor, fog under
    // Fog; never the ambient colours, indirect matrices or alpha references, which stay as the shared list and the
    // state changes since left them (an actor's light loader sets the ambient colour between the two lists).
    void loadRegisters(const J3DMaterialSetup& rSetup, const J3DMaterialValues& rValues) {
        f32* pC = sRegisters;
        const u32 diff = rSetup.diff_flags;
        const bool whole = diff == 0;
        const u16 loads = rSetup.loads;
        if ((whole || (diff & J3DDiffFlag_TevReg) != 0) && (loads & J3D_LOADS_REGISTER_COLOR) != 0) {
            for (u32 r = 0; r < 3; r++) {
                for (u32 c = 0; c < 4; c++) {
                    pC[4 + r * 4 + c] = rValues.register_color[r][c] / 255.0f;
                }
            }
        }
        if ((whole || (diff & J3DDiffFlag_TevReg) != 0) && (loads & J3D_LOADS_KONST_COLOR) != 0) {
            for (u32 k = 0; k < 4; k++) {
                setColor(pC + 16 + k * 4, rValues.konst_color[k], 4);
            }
        }
        for (u32 ch = 0; ch < 2; ch++) {
            if ((whole || (diff & J3DDiffFlag_MatColor) != 0) && (loads & J3D_LOADS_CHANNELS) != 0) {
                setColor(pC + 32 + ch * 4, rValues.material_color[ch], 4);
            }
            if (whole && (loads & J3D_LOADS_AMBIENT) != 0) {
                setColor(pC + 40 + ch * 4, rValues.ambient_color[ch], 4);
            }
        }
        if (whole && (loads & J3D_LOADS_INDIRECT) != 0) {
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
        if ((whole || (diff & J3DDiffFlag_Fog) != 0) && (loads & J3D_LOADS_FOG) != 0) {
            pC[72] = rValues.fog.start;
            pC[73] = rValues.fog.end;
            pC[74] = rValues.fog.near_z;
            pC[75] = rValues.fog.far_z;
            setColor(pC + 76, rValues.fog.color, 3);
            pC[79] = rSetup.fog_type;
        }
        if (whole) {
            pC[80] = rValues.alpha_ref[0] / 255.0f;
            pC[81] = rValues.alpha_ref[1] / 255.0f;
        }
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
        sSiteFollows = sCurrentKind == CURRENT_SITE && (pNative->constants_only & 2) != 0;
        if (sSiteFollows) {
            sSiteRegisters = *pNative;
        }
    }

    u8 nativeFactor(u8 stored, bool source) {
        // The file's colour factors name the other side's colour (a source factor 2 is the destination's colour).
        if (stored == 2 || stored == 3) {
            return source ? static_cast< u8 >(stored == 2 ? NEBULA_FACTOR_DST_COLOR : NEBULA_FACTOR_INV_DST_COLOR)
                          : static_cast< u8 >(stored == 2 ? NEBULA_FACTOR_SRC_COLOR : NEBULA_FACTOR_INV_SRC_COLOR);
        }
        return stored;
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

    // The draw's native material from the list's program and values (j3d_material.hlsli's constants).
    void sendNative(const J3DMaterialSetup& rSetup, const J3DMaterialValues& rValues, u32 shader,
                    bool keepDepthCull = false, const NebulaCookedMaterial* cooked = NULL) {
        // Texture matrices into their places (as the material's data has them).
        const bool differed = rSetup.diff_flags != 0;
        if (!differed || ((rSetup.diff_flags >> 8) & 15) != 0) {
            for (u32 i = 0; i < 8; i++) {
                const u8 id = rSetup.texture_matrix[i].id;
                if (!rSetup.texture_matrix[i].present || (!differed && !rSetup.texture_matrix[i].listed)) {
                    continue;
                }
                const f32(*pMtx)[4] = reinterpret_cast< const f32(*)[4] >(rValues.texture_matrix[i]);
                if (id >= 64) {
                    J3DNativeShape::setPostTexture((id - 64) / 3, pMtx);
                } else if (id >= 30) {
                    J3DNativeShape::setTexture((id - 30) / 3, pMtx, rSetup.texture_matrix[i].rows == 2 ? 2 : 3);
                }
            }
        }
        NebulaNativeMaterial& material = sCurrent;
        // An instance's differed list loads only the textures it changes (diffTexNo); the others stay as the shared
        // list that drew just before loaded them: their images and coordinate scales carry over.
        static NebulaNativeMaterial sShared;
        const bool keepTextures = differed && sCurrentKind == CURRENT_J3D && sCurrent.shader == shader;
        if (keepTextures) {
            memcpy(sShared.textures, material.textures, sizeof(material.textures));
            memcpy(sShared.constants + 84, material.constants + 84, 32 * sizeof(f32));
        }
        memset(&material, 0, sizeof(material));
        material.shader = shader;
        material.blend_op = rSetup.blend.type;  // NEBULA_BLEND_*
        material.blend_src = rSetup.blend.source;  // NEBULA_FACTOR_*
        material.blend_dst = rSetup.blend.destination;
        material.logic_op = rSetup.blend.logic_op;
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
        if (differed) {
            // An instance's differed list carries no culling, and its blend and depth modes only under Blend
            // (J3D diffBlend), as the drawing state takes it (draw_state.cpp set_material): the rest stays as the
            // shared list and the state changes since set it.
            material.cull = NEBULA_NATIVE_KEPT;
            if ((rSetup.diff_flags & J3DDiffFlag_Blend) == 0) {
                material.blend_op = NEBULA_NATIVE_KEPT;
                material.depth_test = NEBULA_NATIVE_KEPT;
            }
        }
        loadRegisters(rSetup, rValues);
        memcpy(material.constants, sRegisters, sizeof(sRegisters));
        // Each coordinate's scale and each map's size (j3d_material.hlsli tex_scale): a sample reads coordinate c
        // of map m at c's scale over m's size.
        for (u32 k = 0; k < 8; k++) {
            f32* pScale = material.constants + 84 + k * 4;
            const NebulaMaterialTexture& rTex = rValues.texture[k];
            if (keepTextures && rTex.image == 0) {
                memcpy(pScale, sShared.constants + 84 + k * 4, 4 * sizeof(f32));
                continue;
            }
            pScale[2] = rTex.width != 0 ? rTex.width : 1.0f;
            pScale[3] = rTex.height != 0 ? rTex.height : 1.0f;
            pScale[0] = rValues.tex_coord_scale[k].scale_s != 0 ? rValues.tex_coord_scale[k].scale_s : pScale[2];
            pScale[1] = rValues.tex_coord_scale[k].scale_t != 0 ? rValues.tex_coord_scale[k].scale_t : pScale[3];
        }
        for (u32 map = 0; map < 8; map++) {
            const NebulaMaterialTexture& rTex = rValues.texture[map];
            if (rTex.image == 0) {
                if (keepTextures) {
                    material.textures[map] = sShared.textures[map];
                }
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
        // Its texgens' post-transform rows (a shape's own texgens replace them at its draw). An instance's differed
        // list loads texgens only when its flags name them (TexGenNum with 0x1000: diffTexGen, as the old path's
        // differed records): otherwise the texgens stay as the draw has them (a shape's own, ShapePacketUserData's
        // environment maps, set before the packet's differed list).
        const bool keepTexGens = differed && !(((rSetup.diff_flags >> 8) & 15) != 0 && (rSetup.diff_flags & 0x1000) != 0);
        for (u32 i = 0; i < 8; i++) {
            material.texgen_post[i] = keepTexGens ? static_cast< u8 >(NEBULA_NATIVE_KEPT)
                                      : i < rSetup.tex_gens
                                          ? static_cast< u8 >(rSetup.tex_gen_post[i] & 0x3F)
                                          : 61;
        }
        // The lights the material's colour block loads (J3DColorBlockLightOn), as its list loaded them: a whole
        // material's with its channels, a differed list's under ColorChan or LightObjNum (diffLight), as the drawing
        // state takes them (native_material.cpp load_matrices).
        const bool lights = differed ? (rSetup.diff_flags & J3DDiffFlag_ColorChan) != 0 ||
                                           ((rSetup.diff_flags >> 4) & 15) != 0
                                     : (rSetup.loads & J3D_LOADS_CHANNELS) != 0;
        if (lights && (rSetup.loads & J3D_LOADS_LIGHTS) != 0) {
            material.light_mask = rValues.light_mask;
            memcpy(material.light, rValues.light, sizeof(material.light));
        }
        J3DNativeShape::flushState();
        J3DNativeState::sendMaterial(&material);
        watchMaterial(&material);
        sCurrentSetup = &rSetup;
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

    // Map `map`'s texture as J3D's loadTexNo sets it up (the image and palette inside the texture's resource).
    void texture(J3DTexture* pTexture, u32 map, u16 texNo, J3DMaterialValues* pOut, u16* pSizes) {
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
        // The texture coordinate scale J3D keeps per map (J3DSys::sTexCoordScaleTable): the image's size.
        pSizes[map * 2] = pImg->mWidth;
        pSizes[map * 2 + 1] = pImg->mHeight;
    }

    // A texture coordinate's scale entry as loadTexCoordScale writes it from its map's table entry.
    void coordScale(J3DMaterialValues* pOut, u32 coord, u32 map) {
        const J3DTexCoordScaleInfo& rInfo = J3DSys::sTexCoordScaleTable[map & 7];
        pOut->tex_coord_scale[coord & 7].scale_s = rInfo.field_0x00;
        pOut->tex_coord_scale[coord & 7].scale_t = rInfo.field_0x02;
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

    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterialParts* pParts) {
        memset(pParts, 0, sizeof(*pParts));
        pParts->cooked = pMaterial->mNativeMaterial;
        J3DMaterialSetup* pOut = &pParts->setup;
        J3DMaterialValues* pVal = &pParts->values;
        J3DRegisterBlock* pTev = pMaterial->mRegisterBlock;
        J3DIndBlock* pInd = pMaterial->mIndBlock;
        J3DTexGenBlock* pTexGen = pMaterial->mTexGenBlock;
        J3DColorBlock* pColor = pMaterial->mColorBlock;
        J3DOutputBlock* pPE = pMaterial->mOutputBlock;

        const u32 indStages = pInd->getIndTexStageNum();
        pOut->tex_gens = static_cast< u8 >(pTexGen->getTexGenNum());
        {
            const u8 mode = static_cast< u8 >(pColor->getCullMode());
            pOut->cull = mode == J3D_CULL_BACK ? NEBULA_CULL_BACK : mode == J3D_CULL_FRONT ? NEBULA_CULL_FRONT
                         : mode == J3D_CULL_ALL ? NEBULA_CULL_ALL : NEBULA_CULL_NONE;
        }

        // The null register block has no textures or orders.
        const bool textured = pTev->hasTextures();
        // Textures first: their sizes fill the coordinate scale table.
        u16 sizes[16];
        memset(sizes, 0, sizeof(sizes));
        for (u32 map = 0; textured && map < 8; map++) {
            const u16 texNo = pTev->getTexNo(map);
            if (texNo != 0xFFFF) {
                texture(pTexture, map, texNo, pVal, sizes);
                J3DSys::sTexCoordScaleTable[map].field_0x00 = sizes[map * 2];
                J3DSys::sTexCoordScaleTable[map].field_0x02 = sizes[map * 2 + 1];
            }
        }
        for (u32 k = 0; pParts->cooked != NULL && k < 8; k++) {
            if (pParts->cooked->coord_map[k] != 255) {
                coordScale(pVal, k, pParts->cooked->coord_map[k]);
            }
        }
        // The parts a register block variant keeps (the one-stage variant has no colours): left as set before.
        if (pTev->getRegisterColor(0) != NULL) {
            pOut->loads |= J3D_LOADS_REGISTER_COLOR;
            for (u32 i = 0; i < 3; i++) {
                const J3DColorWide& rColor = *pTev->getRegisterColor(i);
                pVal->register_color[i][0] = rColor.r;
                pVal->register_color[i][1] = rColor.g;
                pVal->register_color[i][2] = rColor.b;
                pVal->register_color[i][3] = rColor.a;
            }
        }
        if (pTev->getConstantColor(0) != NULL) {
            pOut->loads |= J3D_LOADS_KONST_COLOR;
            for (u32 i = 0; i < 4; i++) {
                copyColor(pVal->konst_color[i], *pTev->getConstantColor(i));
            }
        }

        if (pInd->isFull()) {
            pOut->loads |= J3D_LOADS_INDIRECT;
        }
        for (u32 i = 0; i < indStages && i < 3; i++) {
            const J3DIndTexMtx& rMtx = *pInd->getIndTexMtx(i);
            memcpy(pVal->ind_matrix[i].m, rMtx.field_0x0, sizeof(pVal->ind_matrix[i].m));
            pVal->ind_matrix[i].scale = static_cast< s8 >(rMtx.field_0x18);
        }

        // The post-transform row each coordinate uses (identity unless j3dSys asks for post matrices).
        for (u32 i = 0; i < pOut->tex_gens && i < 8; i++) {
            const J3DTexCoord& rCoord = *pTexGen->getTexCoord(i);
            pOut->tex_gen_post[i] =
                j3dSys.checkFlag(0x40000000) && rCoord.getTexGenMtx() != 60 ? static_cast< u8 >(i * 3) : 61;
        }

        // The null colour block has no channels or colours.
        if (pColor->getColorChan(0) != NULL) {
            pOut->loads |= J3D_LOADS_CHANNELS;
        }
        // Texture matrices as the texgen block's list loads them (J3DTexMtx::load, with this moment's j3dSys flags).
        for (u32 i = 0; i < 8; i++) {
            J3DTexMtx* pMtx = pTexGen->getTexMtx(i);
            if (pMtx == NULL) {
                continue;
            }
            const bool post = j3dSys.checkFlag(J3DSysFlag_PostTexMtx);
            pOut->texture_matrix[i].present = 1;
            pOut->texture_matrix[i].listed = i < pOut->tex_gens && pTexGen->getTexCoord(i)->getTexGenMtx() != J3D_IDENTITY;
            pOut->texture_matrix[i].id = static_cast< u8 >(post ? i * 3 + 0x40 : i * 3 + 30);
            pOut->texture_matrix[i].rows = post || pMtx->getTexMtxInfo().mProjection != J3D_MTX2x4 ? 3 : 2;
            memcpy(pVal->texture_matrix[i], pMtx->getMtx(), sizeof(pVal->texture_matrix[i]));
        }
        for (u32 i = 0; i < 2; i++) {
            if (pColor->getMatColor(i) != NULL) {
                copyColor(pVal->material_color[i], *pColor->getMatColor(i));
            }
            if (pColor->getAmbColor(i) != NULL) {
                pOut->loads |= J3D_LOADS_AMBIENT;
                copyColor(pVal->ambient_color[i], *pColor->getAmbColor(i));
            }
        }
        // Lights only from the blocks that load them (J3DColorBlockLightOn).
        for (u32 i = 0; i < 8 && pColor->getKind() == J3DColorBlock::Kind_LightOn; i++) {
            pOut->loads |= J3D_LOADS_LIGHTS;
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

        // The preset blocks write fixed modes and no fog (J3DPEBlockOpa, TexEdge, Xlu load).
        const J3DOutputBlock::Kind peKind = pPE->getKind();
        if (pPE->isPreset()) {
            const bool edge = peKind == J3DOutputBlock::Kind_TexEdge;
            const bool xlu = peKind == J3DOutputBlock::Kind_Translucent;
            pOut->alpha_compare.comp0 = edge ? NEBULA_COMPARE_GEQUAL : NEBULA_COMPARE_ALWAYS;
            pVal->alpha_ref[0] = edge ? 0x80 : 0;
            pOut->alpha_compare.op = J3D_AOP_AND;
            pOut->alpha_compare.comp1 = edge ? NEBULA_COMPARE_LEQUAL : NEBULA_COMPARE_ALWAYS;
            pVal->alpha_ref[1] = edge ? 0xFF : 0;
            pOut->blend.type = xlu ? NEBULA_BLEND_ADD : NEBULA_BLEND_REPLACE;
            pOut->blend.source = xlu ? NEBULA_FACTOR_SRC_ALPHA : NEBULA_FACTOR_ONE;
            pOut->blend.destination = xlu ? NEBULA_FACTOR_INV_SRC_ALPHA : NEBULA_FACTOR_ZERO;
            pOut->blend.logic_op = J3D_LO_COPY;
            pOut->z_mode.enable = 1;
            pOut->z_mode.func = NEBULA_COMPARE_LEQUAL;
            pOut->z_mode.update = xlu ? 0 : 1;
            pOut->z_compare_location = edge ? 0 : 1;
            return;
        }
        if (J3DFog* pFog = pPE->getFog()) {
            pOut->loads |= J3D_LOADS_FOG;
            const J3DFogInfo& rFog = *pFog->getFogInfo();
            pOut->fog_type = rFog.mType;
            pVal->fog.start = rFog.mStartZ;
            pVal->fog.end = rFog.mEndZ;
            pVal->fog.near_z = rFog.mNearZ;
            pVal->fog.far_z = rFog.mFarZ;
            copyColor(pVal->fog.color, rFog.mColor);
        }
        const J3DAlphaComp& rAlpha = *pPE->getAlphaComp();
        pOut->alpha_compare.comp0 = rAlpha.getComp0();
        pVal->alpha_ref[0] = rAlpha.getRef0();
        pOut->alpha_compare.op = rAlpha.getOp();
        pOut->alpha_compare.comp1 = rAlpha.getComp1();
        pVal->alpha_ref[1] = rAlpha.getRef1();
        const J3DBlend& rBlend = *pPE->getBlend();
        {
            const u8 type = rBlend.mType;
            pOut->blend.type = type == J3D_BM_BLEND ? NEBULA_BLEND_ADD : type == J3D_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
                               : type == J3D_BM_LOGIC ? NEBULA_BLEND_LOGIC : NEBULA_BLEND_REPLACE;
        }
        pOut->blend.source = nativeFactor(rBlend.mSrcFactor, true);
        pOut->blend.destination = nativeFactor(rBlend.mDstFactor, false);
        pOut->blend.logic_op = rBlend.mOp;
        const J3DZMode& rZ = *pPE->getZMode();
        pOut->z_mode.enable = rZ.getCompareEnable();
        pOut->z_mode.func = rZ.getFunc();
        pOut->z_mode.update = rZ.getUpdateEnable();
        pOut->z_compare_location = pPE->getZCompLoc();
    }

    NebulaMaterialParts* create() {
        return new (0x20) NebulaMaterialParts;
    }

    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DMaterialSnapshot* pList, u32 diffFlags) {
        if (pList != NULL && pList->mpNebulaMaterial != NULL) {
            NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
            build(pMaterial, pTexture, &rParts);
            rParts.setup.diff_flags = diffFlags;
            rParts.nativeShader = cookedShaderOf(pMaterial);
            pList->mNebulaCaptured = true;
        }
    }

    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DMaterialSnapshot* pList) {
        static NebulaMaterialParts sParts;
        J3DNativeShape::flushState();
        if (pList != NULL && pList->mNebulaCaptured) {
            const NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
            if (rParts.nativeShader != 0) {
                sendNative(rParts.setup, rParts.values, rParts.nativeShader, false, rParts.cooked);
                return;
            }
            notDrawn(rParts.nativeShader, keyOf(pList));
            return;
        }
        // A list from the model file, which has no snapshot: its material built now, its program with it.
        build(pMaterial, pTexture, &sParts);
        const u32 shader = cookedShaderOf(pMaterial);
        if (shader != 0) {
            sendNative(sParts.setup, sParts.values, shader, false, sParts.cooked);
            return;
        }
        notDrawn(shader, keyOf(pMaterial));
    }

    void sendDiffered(J3DMaterialSnapshot* pList) {
        if (pList == NULL || !pList->mNebulaCaptured) {
            return;
        }
        const NebulaMaterialParts& rParts = *pList->mpNebulaMaterial;
        if (rParts.nativeShader != 0) {
            sendNative(rParts.setup, rParts.values, rParts.nativeShader, false, rParts.cooked);
            return;
        }
        J3DNativeShape::flushState();
        notDrawn(rParts.nativeShader, keyOf(pList));
    }

    const f32* registers() {
        return sRegisters;
    }

    void watch(const NebulaNativeMaterial* pNative) {
        watchMaterial(pNative);
    }

    bool override(u32 mode, f32 a0, f32 a1, f32 ref) {
        if (sCurrentKind != CURRENT_J3D) {
            return false;
        }
        // j3d_material.hlsli MaterialConstants::ovr, after the register file and the coordinate scales.
        f32* pOverride = sCurrent.constants + 84 + 32;
        pOverride[0] = static_cast< f32 >(mode);
        pOverride[1] = a0;
        pOverride[2] = a1;
        pOverride[3] = ref;
        sCurrent.constants_only = 1;
        J3DNativeState::sendMaterial(&sCurrent);
        return true;
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
        // j3d_material.hlsli MaterialConstants::ovr_coord, after ovr; coordinate 0's scale as the new map's size.
        sCurrent.constants[84 + 32 + 4] = 1.0f;
        f32* pScale = sCurrent.constants + 84;
        pScale[0] = pScale[2] = width;
        pScale[1] = pScale[3] = height;
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
        const u32 id = sCurrentCooked->passes[sCurrentPass];
        if (id == 0) {
            nebula_log("cooked material %u has no pass %u\n", sCurrentCooked->material_index, sCurrentPass);
            notDrawn(0, keyOf(sCurrentCooked));
            return;
        }
        sCurrent.shader = NEBULA_SHADER_J3D_FIRST + id - 1;
        memcpy(sCurrent.constants, sRegisters, sizeof(sRegisters));
        if ((pass & (NEBULA_PASS_PLAYER_OPACITY | NEBULA_PASS_LIGHT_MASK | NEBULA_PASS_SHADOW_VOLUME)) != 0) {
            sRegisters[80] = sRegisters[81] = 0;
            sCurrent.constants[80] = sCurrent.constants[81] = 0;
        }
        sCurrent.constants_only = 0;
        J3DNativeState::sendMaterial(&sCurrent);
        watchMaterial(&sCurrent);
    }

    void setColorRegister(u32 index, NebulaColor8 color) {
        setColor8(sRegisters + (index & 3) * 4, color);
    }

    void setColorRegisterWide(u32 index, const NebulaColor16& rColor) {
        f32* pRegister = sRegisters + (index & 3) * 4;
        pRegister[0] = rColor.r / 255.0f;
        pRegister[1] = rColor.g / 255.0f;
        pRegister[2] = rColor.b / 255.0f;
        pRegister[3] = rColor.a / 255.0f;
    }

    void setConstantColor(u32 index, NebulaColor8 color) {
        setColor8(sRegisters + 16 + (index & 3) * 4, color);
    }

    void setLightMaterial(u32 channel, NebulaColor8 color) {
        setColor8(sRegisters + 32 + (channel & 1) * 4, color);
    }

    void setLightAmbient(u32 channel, NebulaColor8 color) {
        setColor8(sRegisters + 40 + (channel & 1) * 4, color);
    }

    void setIndirectMatrix(u32 index, const f32 offset[2][3], s8 scaleExp) {
        f32 scale = 1.0f;
        for (s32 e = scaleExp; e > 0; e--) {
            scale *= 2.0f;
        }
        for (s32 e = scaleExp; e < 0; e++) {
            scale *= 0.5f;
        }
        for (u32 column = 0; column < 3; column++) {
            for (u32 row = 0; row < 2; row++) {
                sRegisters[48 + (index * 2 + row) * 4 + column] = offset[row][column] * scale;
            }
        }
    }

    void setFog(u32 type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
        f32* pC = sRegisters;
        pC[72] = start;
        pC[73] = end;
        pC[74] = nearZ;
        pC[75] = farZ;
        pC[76] = color.r / 255.0f;
        pC[77] = color.g / 255.0f;
        pC[78] = color.b / 255.0f;
        pC[79] = static_cast< f32 >(type);  // j3d_material.hlsli j3d_fog
    }

    void setAlphaTest(u8 compare0, u8 reference0, u8 op, u8 compare1, u8 reference1) {
        // Comparisons other than the current J3D material's: its shader does not compute them, so it ends.
        if (sCurrentKind == CURRENT_J3D &&
            (compare0 != sCurrentSetup->alpha_compare.comp0 || op != sCurrentSetup->alpha_compare.op ||
             compare1 != sCurrentSetup->alpha_compare.comp1)) {
            endCurrent();
        }
        sRegisters[80] = reference0 / 255.0f;
        sRegisters[81] = reference1 / 255.0f;
    }

    void sendRegisters() {
        if (sCurrentKind == CURRENT_SITE && sSiteFollows) {
            memcpy(sSiteRegisters.constants, sRegisters, sizeof(sRegisters));
            sSiteRegisters.constants_only = 3;
            J3DNativeState::sendMaterial(&sSiteRegisters);
        }
        if (sCurrentKind == CURRENT_J3D) {
            memcpy(sCurrent.constants, sRegisters, sizeof(sRegisters));
            sCurrent.constants_only = 1;
            J3DNativeState::sendMaterial(&sCurrent);
        }
    }
};  // namespace J3DNativeMaterial
