#include "Game/Util/FurDrawer.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/GameTexture.hpp"
#include <cmath>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setChannelColor_f1, "setChannelColor.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupMaterial_f1, "setupMaterial.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupMaterial_f2, "setupMaterial.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupMaterial_f3, "setupMaterial.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupLayerMaterial_f1, "setupLayerMaterial.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupLayerMaterial_f2, "setupLayerMaterial.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupLayerMaterial_f3, "setupLayerMaterial.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createFurMap_f1, "createFurMap.f1", 255.0f);

namespace {
    NEBULA_TUNABLE(u32, cFurMapSeed, 1);
    // Shell pixels at or under this alpha (0-255) are not drawn.
    NEBULA_TUNABLE(f32, cAlphaReference, 32.0f);

    // The fur's native material (runtime/src/render/materials/fur.hlsl) as setupMaterial set it; each shell sends it
    // with its own brightness, alphas and reach.
    SiteMaterial sMaterial;

    void setChannelColor(f32* pOut, u8 intensity) {
        pOut[0] = pOut[1] = pOut[2] = intensity / cNum_setChannelColor_f1;
        pOut[3] = 1.0f;
    }
}  // namespace

FurDrawer::FurDrawer(u32 numLayers, ResTIMG* pBody)
    : mBodyTexture(new GameTexture(pBody)), mFurTexture(), mNumLayers(numLayers), mLength(), mShells(new f32[numLayers]), mBrightness(),
      mAlpha(), mColorBlend(), mFurUVScale(1.0f), mBodyUVScale(1.0f), mColor() {
    for (u32 layer = 0; layer < numLayers; layer++) {
        mShells[layer] = 0.0f;
    }
}

f32 FurDrawer::CLayerParam::calcValue(s32 layer, s32 numLayers) const {
    f32 t = pow(f32(layer + 1) / numLayers, mCurve);
    return mRoot + t * (mTip - mRoot);
}

void FurDrawer::setupMaterial(const DynamicFurParam& rParam) const {
    const FurLightParam& rLight = *rParam.mLightParam;
    const bool useSpecular = rLight.mSpecularLightMask != 0;

    sMaterial.init(NEBULA_SHADER_FUR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false, NEBULA_CULL_BACK);
    sMaterial.mState.color_write = 1;
    sMaterial.mState.alpha_write = 0;

    setChannelColor(sMaterial.constants + 0, rLight.mMatIntensity);
    setChannelColor(sMaterial.constants + 4, rLight.mAmbIntensity);
    setChannelColor(sMaterial.constants + 8, rLight.mSpecularMatIntensity);
    setChannelColor(sMaterial.constants + 12, rLight.mSpecularAmbIntensity);
    // Light masks, then the ambient and material sources per channel (a set bit takes the vertex colour).
    sMaterial.constants[24] = static_cast< f32 >(rLight.mLightMask);
    sMaterial.constants[25] = static_cast< f32 >(rLight.mSpecularLightMask);
    sMaterial.constants[26] = static_cast< f32 >(rLight.mColorSource & (useSpecular ? 0xF : 0x3));
    sMaterial.constants[27] = useSpecular ? 1.0f : 0.0f;

    // The fog: start, end, near, far; colour; type.
    const FogInfo& rFog = rParam.mFogCtrl->mFogInfo;
    f32* pFog = sMaterial.constants + 28;
    pFog[0] = rFog.mStartZ;
    pFog[1] = rFog.mEndZ;
    pFog[2] = rFog.mNearZ;
    pFog[3] = rFog.mFarZ;
    pFog[4] = rFog.mColor.r / cNum_setupMaterial_f1;
    pFog[5] = rFog.mColor.g / cNum_setupMaterial_f2;
    pFog[6] = rFog.mColor.b / cNum_setupMaterial_f3;
    pFog[7] = rFog.mType;

    Mtx bodyMtx;
    Mtx furMtx;
    NebulaMtxScale(bodyMtx, mBodyUVScale, mBodyUVScale, 1.0f);
    NebulaMtxScale(furMtx, mFurUVScale, mFurUVScale, 1.0f);
    sMaterial.setMatrix(9, 0, bodyMtx);
    sMaterial.setMatrix(9, 1, furMtx);
    sMaterial.constants[61] = cAlphaReference;

    sMaterial.setTexture(0, mBodyTexture);
    sMaterial.setTexture(1, mFurTexture);
}

void FurDrawer::setupLayerMaterial(s32 layer) const {
    const f32 brightness = mBrightness.calcValue(layer, mNumLayers);
    const f32 alpha = mAlpha.calcValue(mNumLayers - layer - 1, mNumLayers);
    const f32 baseAlpha = mColorBlend.calcValue(mNumLayers - layer - 1, mNumLayers);
    // The shell's brightness, alpha and base alpha as 8-bit values.
    sMaterial.constants[16] = static_cast< u8 >(static_cast< s32 >(brightness)) / cNum_setupLayerMaterial_f1;
    sMaterial.constants[17] = static_cast< u8 >(static_cast< s32 >(alpha)) / cNum_setupLayerMaterial_f2;
    sMaterial.constants[18] = static_cast< u8 >(static_cast< s32 >(baseAlpha)) / cNum_setupLayerMaterial_f3;
    sMaterial.constants[19] = 1.0f;
    sMaterial.setColor(20, mColor.r, mColor.g, mColor.b, 255);
    sMaterial.constants[60] = mShells[layer];
}

const SiteMaterial& FurDrawer::material() const {
    return sMaterial;
}

void FurDrawer::createFurMap(const FurParam& rParam) {
    const u32 width = 32;
    const u32 height = 32;
    if (mFurTexture == nullptr) {
        u32 size = RenderTexture::linearSize(NEBULA_TEXTURE_LINEAR_LA8, width, height, 1);
        ResTIMG* pImage = reinterpret_cast< ResTIMG* >(new u8[size + sizeof(ResTIMG)]);
        pImage->mFormat = NEBULA_TEXTURE_LINEAR_LA8;
        pImage->mWidth = width;
        pImage->mHeight = height;
        pImage->mWrapS = NEBULA_WRAP_REPEAT;
        pImage->mWrapT = NEBULA_WRAP_REPEAT;
        pImage->mImage = nullptr;
        pImage->mMipmap = false;
        pImage->mMaxAnisotropy = 0;
        pImage->mMinType = NEBULA_FILTER_LINEAR;
        pImage->mMagType = NEBULA_FILTER_LINEAR;
        pImage->mMinLod = 0;
        pImage->mMaxLod = 0;
        pImage->mImageNum = 1;
        pImage->mLodBias = 0;
        pImage->mImageDataOffset = sizeof(ResTIMG);
        mFurTexture = new GameTexture(pImage);
    }

    // LA8 texels in rows (NEBULA_TEXTURE_LINEAR_LA8): intensity, then alpha. Each strand is written at its place in the rows.
    u8* pTexels = mFurTexture->image();
    const u32 count = height * width;

    for (u32 i = 0; i < count; i++) {
        pTexels[2 * i] = 255;
        pTexels[2 * i + 1] = 0;
    }

    // Its own sequence: the same parameters give the same strands, and making fur (at scene start, for every power-up
    // form) takes nothing from the game's random numbers.
    FastRandom random(cFurMapSeed);
    for (u32 type = 0; type < 4; type++) {
        for (u32 i = 0; i < f32(height) * f32(width) * rParam.mDensity[type]; i++) {
            f32 randY = random.getRandF();
            f32 randX = random.getRandF();
            s32 y = f32(height) * randY;
            s32 x = f32(width) * randX;

            if (y == s32(height)) {
                y = s32(height) - 1;
            }

            if (x == s32(width)) {
                x = s32(width) - 1;
            }

            const u32 index = x + width * y;
            const u32 tilesAcross = (width + 3) / 4;
            const u32 tile = index >> 4;
            const u32 texelX = (tile % tilesAcross) * 4 + (index & 3);
            const u32 texelY = (tile / tilesAcross) * 4 + ((index >> 2) & 3);
            if (texelX < width && texelY < height) {
                u8* pTexel = pTexels + 2 * (texelX + width * texelY);
                pTexel[0] = static_cast< u8 >(255 - rParam.mLengthMap[type]);
                pTexel[1] = static_cast< u8 >(static_cast< s32 >(cNum_createFurMap_f1 * rParam.mThickness[type]));
            }
        }
    }

    RenderTexture::linear(pTexels, NEBULA_TEXTURE_LINEAR_LA8, width, height, 1);
}
