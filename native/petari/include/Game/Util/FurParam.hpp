#pragma once

#include <NebulaGpuTypes.h>

class FogCtrl;

class FurParam {
public:
    s32 mNumLayers;
    f32 mLength;
    f32 mLengthCurve;
    f32 mIndirect;
    f32 mIndirectCurve;
    f32 mBrightnessTip;
    f32 mBrightnessRoot;
    f32 mBrightnessCurve;
    f32 mAlphaTip;
    f32 mAlphaRoot;
    f32 mAlphaCurve;
    f32 mSkinAlphaTip;
    f32 mSkinAlphaRoot;
    f32 mSkinAlphaCurve;
    f32 mFurUVScale;
    f32 mBodyUVScale;
    NebulaColor8 mColor;
    f32 mDensity[4];
    f32 mThickness[4];
    u8 mLengthMap[4];
};

class FurLightParam {
public:
    FurLightParam()
        : mLightMask(1), mSpecularLightMask(), mMatIntensity(255), mAmbIntensity(50), mSpecularMatIntensity(255), mSpecularAmbIntensity(),
          mLightType(-1) {
        mColorSource = 0;
    }

    u16 mLightMask;
    u16 mSpecularLightMask;
    u8 mMatIntensity;
    u8 mAmbIntensity;
    u8 mSpecularMatIntensity;
    u8 mSpecularAmbIntensity;
    u8 mColorSource;
    s32 mLightType;
};

class DynamicFurParam {
public:
    FogCtrl* mFogCtrl;
    FurLightParam* mLightParam;
};

namespace MR {
    void initFurParamFromFile(FurParam*, DynamicFurParam*, char*, u32);
};  // namespace MR
