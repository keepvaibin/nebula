#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class TripodBossKillerGenerater;

class TripodBossKillerGeneraterCircle : public NameObj {
public:
    TripodBossKillerGeneraterCircle(const char* pName);

    virtual void init(const TableIter&);

    void placementGenerater();

    TripodBossKillerGenerater* mGenerater;
    Vec3f mPosition;
    Vec3f mRotation;
    const f32* mAngleTable;
    s32 mAngleNum;
    f32 mHeight;
    f32 mRadius;
    f32 mCirclePitch;
    f32 mCannonPitch;
};
