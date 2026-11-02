#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class FluffWindEffect : public LiveActor {
public:
    FluffWindEffect(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void makeActorDead();
    virtual void startClipped();

    void initEffectInfo(const TVec3f&, const TVec3f&, const TVec3f&, const char*, f32);

    void exeBrowWind();

    const char* mEffectName;
    TPos3f mMtx;
    s32 mTimer;
};

class FluffWind : public LiveActor {
public:
    FluffWind(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void makeActorDead();

    s32 mNumEffects;
    FluffWindEffect** mEffects;
};

class FluffWindHolder : public LiveActorGroup {
public:
    FluffWindHolder();

    void calcWindInfo(const TVec3f& rPosition, TVec3f* pWindDirection, f32* pWindStrength) const;
};

namespace FluffFunction {
    bool calcFluffWindInfo(const TVec3f& rPosition, TVec3f* pWindDirection, f32* pWindStrength);
};  // namespace FluffFunction
