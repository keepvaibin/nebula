#include "Game/Util/FurDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Util/MathUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cmath>

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
    PSMTXIdentity(mFurTexMtx);
    PSMTXIdentity(mIndirectTexMtx);
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
    PSMTXScale(mFurTexMtx, mFurUVScale, mFurUVScale, 1.0f);
}

f32 FurDrawer::CLayerParam::calcValue(s32 layer, s32 numLayers) const {
    f32 t = pow(f32(layer + 1) / numLayers, mCurve);
    return mRoot + t * (mTip - mRoot);
}

void FurDrawer::setupMaterial(DynamicFurParam* pParam) const {
    bool useSpecular = false;
    FurLightParam* pLight = pParam->mLightParam;

    if (pLight && pLight->mSpecularLightMask) {
        useSpecular = true;
    }

    J3DNativeState::setCoPlanar(GX_FALSE);
    J3DNativeState::setCullMode(static_cast< GXCullMode >(mCullMode));

    if (mAdditive) {
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
    } else {
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    }

    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, mZWrite);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setAlphaCompare(GX_GREATER, mAlphaRef, GX_AOP_OR, GX_GREATER, mAlphaRef);
    J3DNativeState::setZCompLoc(mZCompLoc);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setNumTevStages(2);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3_R);
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K3_G);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_KONST);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K3);
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K3_B);

    if (useSpecular) {
        J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR1A1);
        J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_C2, GX_CC_CPREV, GX_CC_TEXC, GX_CC_RASC);
    } else {
        J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
        J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_C2, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO);
    }

    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_APREV, GX_CA_KONST);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    if (mUseIndirect) {
        J3DNativeState::setNumIndStages(1);
        J3DNativeState::setTevIndirect(GX_TEVSTAGE1, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    } else {
        J3DNativeState::setNumIndStages(0);
        J3DNativeState::setTevDirect(GX_TEVSTAGE1);
    }

    if (pParam->mFogCtrl) {
        const J3DFogInfo& fog = pParam->mFogCtrl->mFogInfo;
        J3DNativeState::setFog(static_cast< GXFogType >(fog.mType), fog.mStartZ, fog.mEndZ, fog.mNearZ, fog.mFarZ, fog.mColor);
    } else if (mMixFog) {
        TDDraw::mixFogColor(mFogPosition, mFogWidth, 0xFF000080);
    } else {
        GXColor color = {0, 0, 0, 0};
        J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, color);
        J3DNativeState::setFogRangeAdj(GX_FALSE, 0, nullptr);
    }

    Mtx bodyMtx;
    Mtx furMtx;
    Mtx indirectMtx;
    PSMTXCopy(mFurTexMtx, furMtx);

    if (mUseIndirect) {
        PSMTXCopy(mIndirectTexMtx, indirectMtx);
    } else {
        PSMTXIdentity(indirectMtx);
    }

    PSMTXScale(bodyMtx, mBodyUVScale, mBodyUVScale, 1.0f);
    J3DNativeShape::sendTexture(bodyMtx, GX_TEXMTX0, GX_MTX2x4);
    J3DNativeShape::sendTexture(furMtx, GX_TEXMTX1, GX_MTX2x4);

    if (mUseIndirect) {
        J3DNativeShape::sendTexture(indirectMtx, GX_TEXMTX2, GX_MTX2x4);
        J3DNativeState::setNumTexGens(3);
        J3DNativeState::setIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD2, GX_TEXMAP2);
        J3DNativeState::setIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
        J3DNativeState::setTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX2, GX_FALSE, GX_PTIDENTITY);
    } else {
        J3DNativeState::setNumTexGens(2);
    }

    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX1, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setClipMode(GX_CLIP_DISABLE);

    if (pLight) {
        u8 numChans = 1;
        J3DNativeState::setChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR0, GX_TRUE, static_cast< GXColorSrc >(pLight->mColorSource & 1),
                      static_cast< GXColorSrc >((pLight->mColorSource >> 1) & 1), pLight->mLightMask, GX_DF_CLAMP, GX_AF_NONE);

        if (useSpecular) {
            J3DNativeState::setChanCtrl(GX_COLOR1, GX_TRUE, static_cast< GXColorSrc >((pLight->mColorSource >> 2) & 1),
                          static_cast< GXColorSrc >((pLight->mColorSource >> 3) & 1), pLight->mSpecularLightMask, GX_DF_NONE, GX_AF_SPEC);
            J3DNativeState::setChanCtrl(GX_ALPHA1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
            numChans = 2;
        } else {
            J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        }

        GXColor matColor = {0, 0, 0, 255};
        matColor.r = pLight->mMatIntensity;
        matColor.g = pLight->mMatIntensity;
        matColor.b = pLight->mMatIntensity;
        GXColor ambColor = {0, 0, 0, 255};
        ambColor.r = pLight->mAmbIntensity;
        ambColor.g = pLight->mAmbIntensity;
        ambColor.b = pLight->mAmbIntensity;
        J3DNativeState::setChanMatColor(GX_COLOR0A0, matColor);
        J3DNativeState::setChanAmbColor(GX_COLOR0A0, ambColor);
        GXColor specularMatColor = {0, 0, 0, 255};
        specularMatColor.r = pLight->mSpecularMatIntensity;
        specularMatColor.g = pLight->mSpecularMatIntensity;
        specularMatColor.b = pLight->mSpecularMatIntensity;
        GXColor specularAmbColor = {0, 0, 0, 255};
        specularAmbColor.r = pLight->mSpecularAmbIntensity;
        specularAmbColor.g = pLight->mSpecularAmbIntensity;
        specularAmbColor.b = pLight->mSpecularAmbIntensity;
        J3DNativeState::setChanMatColor(GX_COLOR1A1, specularMatColor);
        J3DNativeState::setChanAmbColor(GX_COLOR1A1, specularAmbColor);
        J3DNativeState::setNumChans(numChans);
    } else {
        J3DNativeState::setChanCtrl(GX_COLOR0, GX_TRUE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXColor matColor = {0, 0, 0, 255};
        matColor.r = mMatIntensity;
        matColor.g = mMatIntensity;
        matColor.b = mMatIntensity;
        GXColor ambColor = {0, 0, 0, 255};
        ambColor.r = mAmbIntensity;
        ambColor.g = mAmbIntensity;
        ambColor.b = mAmbIntensity;
        J3DNativeState::setChanMatColor(GX_COLOR0A0, matColor);
        J3DNativeState::setChanAmbColor(GX_COLOR0A0, ambColor);
        J3DNativeState::setNumChans(1);
    }

    mBodyTexture->load(GX_TEXMAP0);
    mFurTexture->load(GX_TEXMAP1);

    if (mUseIndirect && mIndirectTexture) {
        mIndirectTexture->load(GX_TEXMAP2);
    }
}

void FurDrawer::setupLayerMaterial(s32 layer) const {
    f32 alpha = mBrightness.calcValue(layer, mNumLayers);
    f32 brightness = mAlpha.calcValue(mNumLayers - layer - 1, mNumLayers);
    f32 blend = mColorBlend.calcValue(mNumLayers - layer - 1, mNumLayers);
    GXColor kColor;
    kColor.r = (s32)(alpha);
    kColor.g = (s32)(brightness);
    kColor.b = (s32)(blend);
    kColor.a = 255;
    J3DNativeState::setTevKColor(GX_KCOLOR3, kColor);
    GXColor color;
    color.r = mColor.r;
    color.g = mColor.g;
    color.b = mColor.b;
    color.a = 255;
    J3DNativeState::setTevColor(GX_TEVREG2, color);

    if (mUseIndirect) {
        f32 scale = mIndirect.calcValue(layer, mNumLayers);
        f32 mtx[2][3];
        mtx[0][0] = scale;
        mtx[0][1] = 0.0f;
        mtx[0][2] = 0.0f;
        mtx[1][0] = 0.0f;
        mtx[1][1] = scale;
        mtx[1][2] = 0.0f;
        J3DNativeState::setIndTexMtx(GX_ITM_0, mtx, 0);
    }
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
