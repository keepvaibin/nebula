#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class DynamicFurParam;
class JUTTexture;
struct ResTIMG;

class FurDrawer {
public:
    class CLayerParam {
    public:
        CLayerParam(f32 tip, f32 curve) : mTip(tip), mCurve(curve) {
        }

        f32 calcValue(s32, s32) const;

        f32 mTip;
        f32 mRoot;
        f32 mCurve;
    };

    FurDrawer(u32, ResTIMG*, ResTIMG*);

    void update();
    void setupMaterial(DynamicFurParam*) const;
    void setupLayerMaterial(s32) const;
    void createFurMap();

    JUTTexture* mBodyTexture;
    JUTTexture* mFurTexture;
    JUTTexture* mIndirectTexture;
    s32 mNumLayers;
    CLayerParam mLength;
    CLayerParam mIndirect;
    CLayerParam mBrightness;
    CLayerParam mAlpha;
    f32 mFurUVScale;
    u32 mMixFog;
    f32 mBodyUVScale;
    f32 mDensity[4];
    f32 mThickness[4];
    u8 mLengthMap[4];
    CLayerParam mColorBlend;
    NebulaColor8 mColor;
    u8 mCullMode;
    u8 mMatIntensity;
    u8 mAmbIntensity;
    u8 mUseIndirect;
    u8 mZCompLoc;
    u8 mZWrite;
    u8 mAdditive;
    u8 mAlphaRef;
    Mtx mFurTexMtx;
    Mtx mIndirectTexMtx;
    TVec3f mFogPosition;
    f32 mFogWidth;
    u8 mUseDensityMap;
};
