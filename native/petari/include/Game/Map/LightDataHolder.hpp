#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Util/NameIndex.hpp"

struct LightInfo {
    NebulaColor8 mColor;
    Vec mPos;
    bool mIsFollowCamera;
};

struct LightInfoCoin {
    LightInfo base;
    NebulaColor8 mSpecularColor;
    f32 mShininess;
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
    LightInfoCoin mCoinLight;
    MR::NameIndex mIndex;  // area light name -> index in mLights, the first of a name
    // Ray tracing (nebula_game_api.h NEBULA_RAY_TRACING_SHADOW_SETS): a shadow light set's lit partner (an index in
    // mLights, or -1), and per light its own record with the partner's actor lights (its planet light kept): what an
    // actor in a shade zone takes while the tracer draws the shade.
    s32* mPartner;
    AreaLightInfo* mLit;
};
