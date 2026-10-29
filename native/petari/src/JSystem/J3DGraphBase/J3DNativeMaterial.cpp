#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <nebula_game_api.h>
#include <nebula_gx_fifo.h>
#include <revolution/gx/GXRegs.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

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
    void texture(J3DTexture* pTexture, u32 map, u16 texNo, NebulaMaterial* pOut, u16* pSizes) {
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
    void coordScale(NebulaMaterial* pOut, u32 coord, u32 map) {
        const J3DTexCoordScaleInfo& rInfo = J3DSys::sTexCoordScaleTable[map & 7];
        pOut->tex_coord_scale[coord & 7].scale_s = rInfo.field_0x00;
        pOut->tex_coord_scale[coord & 7].scale_t = rInfo.field_0x02;
        pOut->tex_coord_scale[coord & 7].bias_s = rInfo.field_0x04 == 1;
        pOut->tex_coord_scale[coord & 7].bias_t = rInfo.field_0x06 == 1;
    }
}  // namespace

namespace J3DNativeMaterial {
    void build(J3DMaterial* pMaterial, J3DTexture* pTexture, NebulaMaterial* pOut) {
        memset(pOut, 0, sizeof(*pOut));
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
                texture(pTexture, map, texNo, pOut, sizes);
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
            coordScale(pOut, rOrder.mTexCoord, rOrder.mTexMap);
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
            coordScale(pOut, rOrder.mTexCoord, rOrder.mTexMap);
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
                pOut->tev_color[i][0] = rColor.r;
                pOut->tev_color[i][1] = rColor.g;
                pOut->tev_color[i][2] = rColor.b;
                pOut->tev_color[i][3] = rColor.a;
            }
        }
        if (pTev->getTevKColor(0) != NULL) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_KONST_COLOR;
            for (u32 i = 0; i < 4; i++) {
                copyColor(pOut->konst_color[i], *pTev->getTevKColor(i));
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
            coordScale(pOut, pOrder->getCoord(), pOrder->getMap());
        }
        for (u32 i = 0; i < pOut->ind_stages && i < 3; i++) {
            const J3DIndTexMtx& rMtx = *pInd->getIndTexMtx(i);
            memcpy(pOut->ind_matrix[i].m, rMtx.field_0x0, sizeof(pOut->ind_matrix[i].m));
            pOut->ind_matrix[i].scale = static_cast< s8 >(rMtx.field_0x18);
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
            memcpy(pOut->texture_matrix[i].m, pMtx->getMtx(), sizeof(pOut->texture_matrix[i].m));
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
                copyColor(pOut->material_color[i], *pColor->getMatColor(i));
            }
            if (pColor->getAmbColor(i) != NULL) {
                pOut->loads |= NEBULA_MATERIAL_LOADS_AMBIENT;
                copyColor(pOut->ambient_color[i], *pColor->getAmbColor(i));
            }
        }
        // Lights only from the blocks that load them (J3DColorBlockLightOn).
        for (u32 i = 0; i < 8 && pColor->getType() == 'CLON'; i++) {
            pOut->loads |= NEBULA_MATERIAL_LOADS_LIGHTS;
            const J3DLightObj* pLight = pColor->getLight(i);
            if (pLight == NULL) {
                continue;
            }
            pOut->light_mask |= 1 << i;
            const J3DLightInfo& rInfo = pLight->mInfo;
            NebulaMaterialLight& rOut = pOut->light[i];
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
            pOut->alpha_compare.ref0 = edge ? 0x80 : 0;
            pOut->alpha_compare.op = GX_AOP_AND;
            pOut->alpha_compare.comp1 = edge ? GX_LEQUAL : GX_ALWAYS;
            pOut->alpha_compare.ref1 = edge ? 0xFF : 0;
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
            pOut->fog.start = rFog.mStartZ;
            pOut->fog.end = rFog.mEndZ;
            pOut->fog.near_z = rFog.mNearZ;
            pOut->fog.far_z = rFog.mFarZ;
            copyColor(pOut->fog.color, rFog.mColor);
            for (u32 i = 0; i < 10; i++) {
                pOut->fog.adjust[i] = rFog.mFogAdjTable.r[i];
            }
        }
        const J3DAlphaComp& rAlpha = *pPE->getAlphaComp();
        pOut->alpha_compare.comp0 = rAlpha.getComp0();
        pOut->alpha_compare.ref0 = rAlpha.getRef0();
        pOut->alpha_compare.op = rAlpha.getOp();
        pOut->alpha_compare.comp1 = rAlpha.getComp1();
        pOut->alpha_compare.ref1 = rAlpha.getRef1();
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

    NebulaMaterial* create() {
        return new (0x20) NebulaMaterial;
    }

    void capture(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList, u32 diffFlags) {
        if (pList != NULL && pList->mpNebulaMaterial != NULL) {
            build(pMaterial, pTexture, pList->mpNebulaMaterial);
            pList->mpNebulaMaterial->diff_flags = diffFlags;
            pList->mpNebulaMaterial->origin = diffFlags != 0 ? 2 : pTexture == j3dSys.getTexture() ? 3 : 1;
            pList->mNebulaCaptured = true;
        }
    }

    void send(J3DMaterial* pMaterial, J3DTexture* pTexture, J3DDisplayListObj* pList) {
        static NebulaMaterial sMaterial;
        const NebulaMaterial* pSnapshot = pList != NULL && pList->mNebulaCaptured ? pList->mpNebulaMaterial : NULL;
        if (pSnapshot == NULL) {
            build(pMaterial, pTexture, &sMaterial);
            pSnapshot = &sMaterial;
        }
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->material(pHost->user, __NebulaGXWriter.ptr, pSnapshot);
    }

    void sendDiffered(J3DDisplayListObj* pList) {
        if (pList == NULL || !pList->mNebulaCaptured) {
            return;
        }
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->material(pHost->user, __NebulaGXWriter.ptr, pList->mpNebulaMaterial);
    }
};  // namespace J3DNativeMaterial
