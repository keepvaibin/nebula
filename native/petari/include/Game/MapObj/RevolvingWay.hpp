#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class RevolvingWay : public LiveActor {
public:
    RevolvingWay(const char*);

    virtual ~RevolvingWay();
    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void addAccelMoment();

    /* 0x0x8C */ Quat4f mRotateQuat;
    /* 0x0x9C */ Vec3f mFriction;
    /* 0x0xA8 */ f32 mRadius;
};
