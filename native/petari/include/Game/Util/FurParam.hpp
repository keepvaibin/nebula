#pragma once

#include <NebulaGpuTypes.h>

class FogCtrl;

class FurParam {
public:
    s32 mNumLayers;
    f32 mLength;
    f32 mLengthCurve;
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

// A material's <name>.fur.txt as the cook writes it (cook/fur.rs).
struct FurFile {
    enum Value {
        cNumLayers,
        cFirstFloat,
        cColor = cFirstFloat + 13,
        cDensity,
        cThickness,
        cLengthMap,
    };

    char mMagic[4];
    // A bit per Value the file gave; FurParam keeps its default for the others.
    u32 mPresent;
    s32 mNumLayers;
    // mLength through mBodyUVScale in FurParam's order, without the indirect pair.
    f32 mFloats[13];
    u8 mColor[4];
    f32 mDensity[4];
    f32 mThickness[4];
    u8 mLengthMap[4];
    // Light 0 switch, material, ambient, light 1 switch, material, ambient, colour source; 0 when the file lacks one.
    s32 mLight[7];
};

static_assert(sizeof(FurFile) == 132);

namespace MR {
    void applyFurFile(FurParam*, FurLightParam*, const FurFile&);
};  // namespace MR
