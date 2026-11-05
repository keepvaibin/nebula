#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ShootingStar : public LiveActor {
public:
    ShootingStar(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();

    void appearPreShooting();
    void exePreShooting();
    void exeShooting();
    void exeWaitForNextShoot();

    Vec3f mBasePos;
    Vec3f mFallAxis;
    Vec3f mGravityPreShoot;
    s32 mNumBurstStarPiece;
    s32 mWaitForNextShootTime;
    f32 mFallHeight;
};
