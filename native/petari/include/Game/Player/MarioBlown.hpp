#pragma once

#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"

class MarioBlown : public MarioState {
public:
    MarioBlown(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    f32 setVelocityOffGravity(const Vec3f& rVec) {
        return MR::vecKillElement(rVec, mActor->mGravityVec, &mVelocity);
    }

    u16 mTimer;
    u16 mStep;  // update's step
    Vec3f mVelocity;  // the blow's velocity
    bool mJumpedOut;  // left by a jump or a damage floor: the jump stays at close
    bool mAirLanding;  // left the ground in the landing: its animation stops at close
};
