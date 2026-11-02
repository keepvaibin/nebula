#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

struct LightInfo {
    NebulaColor8 mColor;
    Vec mPos;
    bool mIsFollowCamera;
};

struct LightInfoCoin {
    LightInfo base;
    struct {
        NebulaColor8 _14;
        f32 _18;
    };
};

struct ActorLightInfo {
    LightInfo mInfo0;
    LightInfo mInfo1;
    u8 mAlpha2;
    NebulaColor8 mColor;
};

struct AreaLightInfo {
    const char* mAreaLightName;
    s32 mInterpolate;
    bool mFix;
    ActorLightInfo mPlayerLight;
    ActorLightInfo mStrongLight;
    ActorLightInfo mWeakLight;
    ActorLightInfo mPlanetLight;
};

class LightDataHolder {
public:
    LightDataHolder();

    void initLightData();

    AreaLightInfo* findAreaLight(const char*) const;
    const char* getDefaultAreaLightName() const;
    s32 getDefaultStepInterpolate() const;

    inline AreaLightInfo* getLightInfo(s32 idx) const {
        return &mLights[idx];
    }

    s32 mLightCount;
    AreaLightInfo* mLights;
    LightInfoCoin _8;
};
