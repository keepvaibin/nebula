#include "Game/Util/FurDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cmath>
#include <cstring>

FurDrawer::FurDrawer(u32 numLayers, ResTIMG* pBody, ResTIMG* pIndirect)
    : mBodyTexture(), mFurTexture(), mIndirectTexture(), mNumLayers(numLayers), mLength(3.0f, 1.0f), mIndirect(0.2f, 1.5f), mBrightness(40.0f, 4.5f),
      mAlpha(210.0f, 1.8f), mFurUVScale(1.0f), mBodyUVScale(1.0f), mColorBlend(0.0f, 1.0f), mCullMode(GX_CULL_BACK), mMatIntensity(255),
      mAmbIntensity(50), mUseIndirect(1), mZCompLoc(), mZWrite(), mAdditive(), mAlphaRef(32) {
    mBodyTexture = new JUTTexture(pBody, 0);

    if (mIndirectTexture) {
        mIndirectTexture = new JUTTexture(pIndirect, 0);
        mUseIndirect = 1;
    } else {
        mUseIndirect = 0;
    }

    u32 size = J3DNativeTexture::imageSize(GX_TF_IA8, 32, 32, 1);
    ResTIMG* pImage = reinterpret_cast< ResTIMG* >(new (32) u8[size + sizeof(ResTIMG)]);
    pImage->mFormat = GX_TF_IA8;
    pImage->mWidth = 32;
    pImage->mHeight = 32;
    pImage->mWrapS = GX_REPEAT;
    pImage->mWrapT = GX_REPEAT;
    pImage->mPaletteName = GX_TLUT0;
    pImage->mPaletteFormat = GX_TL_IA8;
    pImage->mPaletteNum = 0;
    pImage->mPaletteDataOffset = 0;
    pImage->mMipmap = false;
    pImage->mDoEdgeLod = false;
    pImage->mBiasClamp = false;
    pImage->mMaxAnisotropy = GX_ANISO_1;
    pImage->mMinType = GX_LINEAR;
    pImage->mMagType = GX_LINEAR;
    pImage->mMinLod = 0;
    pImage->mMaxLod = 0;
    pImage->mImageNum = 1;
    pImage->mLodBias = 0;
    pImage->mImageDataOffset = sizeof(ResTIMG);
    mFurTexture = new JUTTexture(pImage, 0);
    mUseDensityMap = 0;
    NebulaMtxIdentity(mFurTexMtx);
    NebulaMtxIdentity(mIndirectTexMtx);
    mMixFog = 0;
    mFogPosition.x = mFogPosition.y = mFogPosition.z = 0.0f;
    mFogWidth = 0.0f;
    mColor.r = 0;
    mColor.g = 0;
    mColor.b = 0;
    mColor.a = 0;
    mDensity[0] = 0.8f;
    mThickness[0] = 0.517f;
    mLengthMap[0] = 26;
    mDensity[1] = 0.6f;
    mThickness[1] = 0.386f;
    mLengthMap[1] = 64;
    mDensity[2] = 0.3f;
    mThickness[2] = 0.3645f;
    mLengthMap[2] = 98;
    mDensity[3] = 0.09f;
    mThickness[3] = 0.1713f;
    mLengthMap[3] = 212;
    createFurMap();
    update();
}

void FurDrawer::update() {
    NebulaMtxScale(mFurTexMtx, mFurUVScale, mFurUVScale, 1.0f);
}

f32 FurDrawer::CLayerParam::calcValue(s32 layer, s32 numLayers) const {
    f32 t = pow(f32(layer + 1) / numLayers, mCurve);
    return mRoot + t * (mTip - mRoot);
}

namespace {
    // The fur's material (setupMaterial) as a material program and values, drawn by its generated native shader
    // (J3DNativeMaterial::sendWhole); each layer sends it again with its own constant colour and indirect scale.
    NebulaMaterialProgram sProgram;
    NebulaMaterialValues sValues;

    void setStage(NebulaMaterialTevStage& rStage, u8 coord, u8 map, u8 channel, u8 c0, u8 c1, u8 c2, u8 c3, u8 colorScale,
                  u8 a0, u8 a1, u8 a2, u8 a3, u8 konstColor, u8 konstAlpha) {
        rStage.tex_coord = coord;
        rStage.tex_map = map;
        rStage.channel = channel;
        rStage.color_in[0] = c0;
        rStage.color_in[1] = c1;
        rStage.color_in[2] = c2;
        rStage.color_in[3] = c3;
        rStage.color_op = GX_TEV_ADD;
        rStage.color_bias = GX_TB_ZERO;
        rStage.color_scale = colorScale;
        rStage.color_clamp = GX_TRUE;
        rStage.color_reg = GX_TEVPREV;
        rStage.alpha_in[0] = a0;
        rStage.alpha_in[1] = a1;
        rStage.alpha_in[2] = a2;
        rStage.alpha_in[3] = a3;
        rStage.alpha_op = GX_TEV_ADD;
        rStage.alpha_bias = GX_TB_ZERO;
        rStage.alpha_scale = GX_CS_SCALE_1;
        rStage.alpha_clamp = GX_TRUE;
        rStage.alpha_reg = GX_TEVPREV;
        rStage.konst_color = konstColor;
        rStage.konst_alpha = konstAlpha;
    }

    void setChannel(NebulaMaterialProgram& rProgram, u32 index, u8 enable, u8 materialSource, u8 ambientSource, u8 lightMask,
                    u8 diffuse, u8 attenuation) {
        rProgram.channel[index].enable = enable;
        rProgram.channel[index].material_source = materialSource;
        rProgram.channel[index].ambient_source = ambientSource;
        rProgram.channel[index].light_mask = lightMask;
        rProgram.channel[index].diffuse = diffuse;
        rProgram.channel[index].attenuation = attenuation;
    }

    void setGrey(u8* pOut, u8 intensity) {
        pOut[0] = intensity;
        pOut[1] = intensity;
        pOut[2] = intensity;
        pOut[3] = 255;
    }
}  // namespace

// The fur's material: stage 0 the body texture lit by channel 0 plus the layer's brightness (konst 3's red), alpha the
// layer's alpha (konst 3's green); stage 1 the fur map over it, moved by the indirect texture, times two and tinted by
// register 2 by the layer's blend (konst 3's blue), plus the specular channel where the light has one; alpha the
// strand's thickness times the layer's alpha. Its lights stay as the actor's light loader set them.
void FurDrawer::setupMaterial(DynamicFurParam* pParam) const {
    bool useSpecular = false;
    FurLightParam* pLight = pParam->mLightParam;

    if (pLight && pLight->mSpecularLightMask) {
        useSpecular = true;
    }

    J3DNativeState::setCoPlanar(GX_FALSE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);

    NebulaMaterialProgram& rProgram = sProgram;
    NebulaMaterialValues& rValues = sValues;
    memset(&rProgram, 0, sizeof(rProgram));
    memset(&rValues, 0, sizeof(rValues));
    rProgram.loads = NEBULA_MATERIAL_LOADS_FOG | NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_TEV_COLOR |
                     NEBULA_MATERIAL_LOADS_KONST_COLOR | NEBULA_MATERIAL_LOADS_KONST_SELECT | NEBULA_MATERIAL_LOADS_SWAP_TABLE |
                     NEBULA_MATERIAL_LOADS_CHANNELS | NEBULA_MATERIAL_LOADS_AMBIENT |
                     (mUseIndirect ? NEBULA_MATERIAL_LOADS_INDIRECT : 0);
    rProgram.cull = static_cast< u8 >(mCullMode);
    rProgram.dither = GX_TRUE;
    rProgram.z_compare_location = mZCompLoc;
    rProgram.tev_stages = 2;
    rProgram.tex_gens = mUseIndirect ? 3 : 2;
    rProgram.ind_stages = mUseIndirect ? 1 : 0;
    for (u32 s = 0; s < 4; s++) {
        // GX's default swap tables (RGBA, RRRA, GGGA, BBBA); the fur's stages use the first.
        rProgram.swap_table[s][0] = s == 0 ? 0 : s - 1;
        rProgram.swap_table[s][1] = s == 0 ? 1 : s - 1;
        rProgram.swap_table[s][2] = s == 0 ? 2 : s - 1;
        rProgram.swap_table[s][3] = 3;
    }

    setStage(rProgram.tev[0], GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_KONST,
             GX_CS_SCALE_1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_TEV_KCSEL_K3_R, GX_TEV_KASEL_K3_G);
    setStage(rProgram.tev[1], GX_TEXCOORD1, GX_TEXMAP1, useSpecular ? GX_COLOR1A1 : GX_COLOR_NULL, GX_CC_C2, GX_CC_CPREV,
             GX_CC_TEXC, useSpecular ? GX_CC_RASC : GX_CC_ZERO, GX_CS_SCALE_2, GX_CA_ZERO, GX_CA_TEXA, GX_CA_APREV,
             GX_CA_KONST, GX_TEV_KCSEL_K3, GX_TEV_KASEL_K3_B);
    if (mUseIndirect) {
        NebulaMaterialTevStage& rStage = rProgram.tev[1];
        rStage.ind_stage = GX_INDTEXSTAGE0;
        rStage.ind_format = GX_ITF_8;
        rStage.ind_bias = GX_ITB_STU;
        rStage.ind_matrix = GX_ITM_0;
        rStage.ind_wrap_s = GX_ITW_OFF;
        rStage.ind_wrap_t = GX_ITW_OFF;
        rStage.ind_prev = GX_FALSE;
        rStage.ind_lod = GX_FALSE;
        rStage.ind_alpha = GX_ITBA_OFF;
        rProgram.ind_order[0].coord = GX_TEXCOORD2;
        rProgram.ind_order[0].map = GX_TEXMAP2;
        rProgram.ind_order[0].scale_s = GX_ITS_1;
        rProgram.ind_order[0].scale_t = GX_ITS_1;
    }

    static const u8 kTexMtx[3] = {GX_TEXMTX0, GX_TEXMTX1, GX_TEXMTX2};
    for (u32 g = 0; g < rProgram.tex_gens; g++) {
        rProgram.tex_gen[g].type = GX_TG_MTX2x4;
        rProgram.tex_gen[g].source = GX_TG_TEX0;
        rProgram.tex_gen[g].matrix = kTexMtx[g];
        rProgram.tex_gen[g].normalize = GX_FALSE;
        rProgram.tex_gen[g].post_matrix = GX_PTIDENTITY;
    }

    if (pLight) {
        setChannel(rProgram, 0, GX_TRUE, pLight->mColorSource & 1, (pLight->mColorSource >> 1) & 1, pLight->mLightMask,
                   GX_DF_CLAMP, GX_AF_NONE);
        setChannel(rProgram, 1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        if (useSpecular) {
            setChannel(rProgram, 2, GX_TRUE, (pLight->mColorSource >> 2) & 1, (pLight->mColorSource >> 3) & 1,
                       pLight->mSpecularLightMask, GX_DF_NONE, GX_AF_SPEC);
        } else {
            setChannel(rProgram, 2, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        }
        setChannel(rProgram, 3, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        rProgram.channels = useSpecular ? 2 : 1;
        setGrey(rValues.material_color[0], pLight->mMatIntensity);
        setGrey(rValues.ambient_color[0], pLight->mAmbIntensity);
        setGrey(rValues.material_color[1], pLight->mSpecularMatIntensity);
        setGrey(rValues.ambient_color[1], pLight->mSpecularAmbIntensity);
    } else {
        setChannel(rProgram, 0, GX_TRUE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
        setChannel(rProgram, 1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        setChannel(rProgram, 2, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        setChannel(rProgram, 3, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        rProgram.channels = 1;
        setGrey(rValues.material_color[0], mMatIntensity);
        setGrey(rValues.ambient_color[0], mAmbIntensity);
    }

    rProgram.alpha_compare.comp0 = GX_GREATER;
    rProgram.alpha_compare.op = GX_AOP_OR;
    rProgram.alpha_compare.comp1 = GX_GREATER;
    rValues.alpha_ref[0] = mAlphaRef;
    rValues.alpha_ref[1] = mAlphaRef;
    rProgram.blend.type = GX_BM_BLEND;
    rProgram.blend.source = GX_BL_SRCALPHA;
    rProgram.blend.destination = mAdditive ? GX_BL_ONE : GX_BL_INVSRCALPHA;
    rProgram.blend.logic_op = GX_LO_CLEAR;
    rProgram.z_mode.enable = GX_TRUE;
    rProgram.z_mode.func = GX_LEQUAL;
    rProgram.z_mode.update = mZWrite;

    if (pParam->mFogCtrl) {
        const J3DFogInfo& rFog = pParam->mFogCtrl->mFogInfo;
        rProgram.fog.type = rFog.mType;
        rValues.fog.start = rFog.mStartZ;
        rValues.fog.end = rFog.mEndZ;
        rValues.fog.near_z = rFog.mNearZ;
        rValues.fog.far_z = rFog.mFarZ;
        rValues.fog.color[0] = rFog.mColor.r;
        rValues.fog.color[1] = rFog.mColor.g;
        rValues.fog.color[2] = rFog.mColor.b;
        rValues.fog.color[3] = rFog.mColor.a;
    } else if (mMixFog) {
        // TDDraw's mixed fog: linear over the sphere around the fog position, the colour 0xFF000080.
        rProgram.fog.type = GX_FOG_PERSP_LIN;
        MR::calcFogStartEnd(mFogPosition, mFogWidth, &rValues.fog.start, &rValues.fog.end);
        rValues.fog.near_z = MR::getNearZ();
        rValues.fog.far_z = MR::getFarZ();
        rValues.fog.color[0] = 0xFF;
        rValues.fog.color[3] = 0x80;
    } else {
        rProgram.fog.type = GX_FOG_NONE;
    }

    Mtx bodyMtx;
    Mtx furMtx;
    Mtx indirectMtx;
    NebulaMtxCopy(mFurTexMtx, furMtx);

    if (mUseIndirect) {
        NebulaMtxCopy(mIndirectTexMtx, indirectMtx);
    } else {
        NebulaMtxIdentity(indirectMtx);
    }

    NebulaMtxScale(bodyMtx, mBodyUVScale, mBodyUVScale, 1.0f);
    J3DNativeShape::setTexture(0, bodyMtx, 2);
    J3DNativeShape::setTexture(1, furMtx, 2);

    if (mUseIndirect) {
        J3DNativeShape::setTexture(2, indirectMtx, 2);
    }

    J3DNativeState::setClipMode(GX_CLIP_DISABLE);
    NativeShader::setMaterialTexture(rValues, 0, 0, mBodyTexture);
    NativeShader::setMaterialTexture(rValues, 1, 1, mFurTexture);

    if (mUseIndirect && mIndirectTexture) {
        NativeShader::setMaterialTexture(rValues, 2, 2, mIndirectTexture);
    }
}

void FurDrawer::setupLayerMaterial(s32 layer) const {
    f32 alpha = mBrightness.calcValue(layer, mNumLayers);
    f32 brightness = mAlpha.calcValue(mNumLayers - layer - 1, mNumLayers);
    f32 blend = mColorBlend.calcValue(mNumLayers - layer - 1, mNumLayers);
    NebulaMaterialValues& rValues = sValues;
    rValues.konst_color[3][0] = static_cast< u8 >((s32)(alpha));
    rValues.konst_color[3][1] = static_cast< u8 >((s32)(brightness));
    rValues.konst_color[3][2] = static_cast< u8 >((s32)(blend));
    rValues.konst_color[3][3] = 255;
    rValues.tev_color[2][0] = mColor.r;
    rValues.tev_color[2][1] = mColor.g;
    rValues.tev_color[2][2] = mColor.b;
    rValues.tev_color[2][3] = 255;

    if (mUseIndirect) {
        f32 scale = mIndirect.calcValue(layer, mNumLayers);
        rValues.ind_matrix[0].m[0][0] = scale;
        rValues.ind_matrix[0].m[0][1] = 0.0f;
        rValues.ind_matrix[0].m[0][2] = 0.0f;
        rValues.ind_matrix[0].m[1][0] = 0.0f;
        rValues.ind_matrix[0].m[1][1] = scale;
        rValues.ind_matrix[0].m[1][2] = 0.0f;
        rValues.ind_matrix[0].scale = 0;
    }

    J3DNativeMaterial::sendWhole(sProgram, rValues);
}

void FurDrawer::createFurMap() {
    u32 count;
    // IA8 texels for GX, in the console's byte order.
    NEBULA_BE(u16)* pPixels;
    u32 width = mFurTexture->getTexInfo()->mWidth;
    u32 height = mFurTexture->getTexInfo()->mHeight;
    pPixels = reinterpret_cast< NEBULA_BE(u16)* >(mFurTexture->mImage);
    count = height * width;

    for (u32 i = 0; i < count; i++) {
        pPixels[i] = 255;
    }

    for (u32 type = 0; type < 4; type++) {
        for (u32 i = 0; i < f32(height) * f32(width) * mDensity[type]; i++) {
            f32 randY = MR::getRandom(0.0f, 1.0f);
            f32 randX = MR::getRandom(0.0f, 1.0f);
            s32 y = f32(height) * randY;
            s32 x = f32(width) * randX;

            if (y == s32(height)) {
                y = s32(height) - 1;
            }

            if (x == s32(width)) {
                x = s32(width) - 1;
            }

            pPixels[x + width * y] = (u16((s32)(255.0f * mThickness[type])) << 8) | u16(255 - mLengthMap[type]);
        }
    }

    J3DNativeTexture::registerImage(pPixels, GX_TF_IA8, width, height, 1);
}
