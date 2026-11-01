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
    // The fur's native material (runtime/src/render/materials/fur.hlsl) as setupMaterial set it; each shell sends it
    // with its own brightness, alphas and indirect scale.
    NebulaNativeMaterial sMaterial;

    void setChannelColor(f32* pOut, u8 intensity) {
        pOut[0] = pOut[1] = pOut[2] = intensity / 255.0f;
        pOut[3] = 1.0f;
    }
}  // namespace

void FurDrawer::setupMaterial(DynamicFurParam* pParam) const {
    bool useSpecular = false;
    FurLightParam* pLight = pParam->mLightParam;

    if (pLight && pLight->mSpecularLightMask) {
        useSpecular = true;
    }

    J3DNativeState::setCoPlanar(GX_FALSE);
    NativeShader::init(sMaterial, NEBULA_SHADER_FUR, mAdditive ? NativeShader::BLEND_ADD_ALPHA : NativeShader::BLEND_ALPHA,
                       NEBULA_COMPARE_LEQUAL, mZWrite != 0,
                       mCullMode == GX_CULL_BACK ? NEBULA_CULL_BACK : mCullMode == GX_CULL_FRONT ? NEBULA_CULL_FRONT
                       : mCullMode == GX_CULL_ALL ? NEBULA_CULL_BACK : NEBULA_CULL_NONE);
    sMaterial.early_depth = mZCompLoc != 0 ? 1 : 0;
    sMaterial.color_write = 1;
    sMaterial.alpha_write = 0;

    // The fog (start, end, near, far; colour; GX type): the fog control's, or TDDraw's mixed fog (linear over the
    // sphere around the fog position, colour 0xFF000080), or none.
    f32* pFog = sMaterial.constants + 32;
    if (pParam->mFogCtrl) {
        const J3DFogInfo& rFog = pParam->mFogCtrl->mFogInfo;
        pFog[0] = rFog.mStartZ;
        pFog[1] = rFog.mEndZ;
        pFog[2] = rFog.mNearZ;
        pFog[3] = rFog.mFarZ;
        pFog[4] = rFog.mColor.r / 255.0f;
        pFog[5] = rFog.mColor.g / 255.0f;
        pFog[6] = rFog.mColor.b / 255.0f;
        pFog[7] = rFog.mType;
    } else if (mMixFog) {
        MR::calcFogStartEnd(mFogPosition, mFogWidth, &pFog[0], &pFog[1]);
        pFog[2] = MR::getNearZ();
        pFog[3] = MR::getFarZ();
        pFog[4] = 1.0f;
        pFog[5] = 0.0f;
        pFog[6] = 0.0f;
        pFog[7] = GX_FOG_PERSP_LIN;
    } else {
        pFog[7] = GX_FOG_NONE;
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

    u32 sources;
    u32 mask0 = 0;
    u32 mask1 = 0;
    if (pLight) {
        // GXSetChanCtrl's ambient source then material source per channel (GX_SRC_VTX 1).
        sources = (pLight->mColorSource & 1) | (((pLight->mColorSource >> 1) & 1) << 1);
        mask0 = pLight->mLightMask;
        if (useSpecular) {
            sources |= (((pLight->mColorSource >> 2) & 1) << 2) | (((pLight->mColorSource >> 3) & 1) << 3);
            mask1 = pLight->mSpecularLightMask;
        }
        setChannelColor(sMaterial.constants + 0, pLight->mMatIntensity);
        setChannelColor(sMaterial.constants + 4, pLight->mAmbIntensity);
        setChannelColor(sMaterial.constants + 8, pLight->mSpecularMatIntensity);
        setChannelColor(sMaterial.constants + 12, pLight->mSpecularAmbIntensity);
    } else {
        // Channel 0 lit by no lights with both colours from the vertex.
        sources = 3;
        setChannelColor(sMaterial.constants + 0, mMatIntensity);
        setChannelColor(sMaterial.constants + 4, mAmbIntensity);
        setChannelColor(sMaterial.constants + 8, 0);
        setChannelColor(sMaterial.constants + 12, 0);
    }
    sMaterial.constants[24] = static_cast< f32 >(mask0);
    sMaterial.constants[25] = static_cast< f32 >(mask1);
    sMaterial.constants[26] = static_cast< f32 >(sources);
    sMaterial.constants[27] = static_cast< f32 >((useSpecular ? 1 : 0) | (mUseIndirect ? 2 : 0));
    sMaterial.constants[29] = mAlphaRef;
    sMaterial.constants[30] = static_cast< f32 >(mFurTexture->getWidth());
    sMaterial.constants[31] = static_cast< f32 >(mFurTexture->getHeight());

    NativeShader::setTexture(sMaterial, 0, mBodyTexture);
    NativeShader::setTexture(sMaterial, 1, mFurTexture);

    if (mUseIndirect && mIndirectTexture) {
        NativeShader::setTexture(sMaterial, 2, mIndirectTexture);
    }
}

void FurDrawer::setupLayerMaterial(s32 layer) const {
    f32 alpha = mBrightness.calcValue(layer, mNumLayers);
    f32 brightness = mAlpha.calcValue(mNumLayers - layer - 1, mNumLayers);
    f32 blend = mColorBlend.calcValue(mNumLayers - layer - 1, mNumLayers);
    // GX's constant colour 3 as the shell's (8-bit channels).
    sMaterial.constants[16] = static_cast< u8 >((s32)(alpha)) / 255.0f;
    sMaterial.constants[17] = static_cast< u8 >((s32)(brightness)) / 255.0f;
    sMaterial.constants[18] = static_cast< u8 >((s32)(blend)) / 255.0f;
    sMaterial.constants[19] = 1.0f;
    NativeShader::setColor(sMaterial, 20, mColor.r, mColor.g, mColor.b, 255);
    sMaterial.constants[28] = mUseIndirect ? mIndirect.calcValue(layer, mNumLayers) : 0.0f;
    NativeShader::send(sMaterial);
}

void FurDrawer::createFurMap() {
    u32 count;
    // Linear luminance-alpha texels in rows (nebula_cooked_texture.h): luminance 255 minus the strand's length, alpha
    // its thickness; no strand is luminance 255, alpha 0.
    u8* pPixels;
    u32 width = mFurTexture->getTexInfo()->mWidth;
    u32 height = mFurTexture->getTexInfo()->mHeight;
    pPixels = static_cast< u8* >(mFurTexture->mImage);
    count = height * width;

    for (u32 i = 0; i < count; i++) {
        pPixels[2 * i] = 255;
        pPixels[2 * i + 1] = 0;
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

            pPixels[2 * (x + width * y)] = static_cast< u8 >(255 - mLengthMap[type]);
            pPixels[2 * (x + width * y) + 1] = static_cast< u8 >((s32)(255.0f * mThickness[type]));
        }
    }

    J3DNativeTexture::registerImage(pPixels, NEBULA_TEXTURE_LINEAR_LA8, width, height, 1);
}
