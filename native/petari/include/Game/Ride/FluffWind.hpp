#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class FluffWindEffect : public LiveActor {
public:
    FluffWindEffect(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void makeActorDead() override;

    // radius <= 0 keeps the default clipping.
    void initEffectInfo(const Vec3f& rPos, const Vec3f& rFront, const Vec3f& rUp, const char* pEffectName, f32 radius);

    void exeBlowWind();

    const char* mEffectName;
    Pos3f mMtx;
    s32 mTimer;
};

class FluffWind : public LiveActor {
public:
    FluffWind(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void makeActorDead() override;

    s32 mNumEffects;
    FluffWindEffect** mEffects;
};

class FluffWindHolder : public LiveActorGroup {
public:
    FluffWindHolder();

    void calcWindInfo(const Vec3f& rPosition, Vec3f* pWindDirection, f32* pWindStrength) const;
};

namespace FluffFunction {
    bool calcFluffWindInfo(const Vec3f& rPosition, Vec3f* pWindDirection, f32* pWindStrength);
};  // namespace FluffFunction
