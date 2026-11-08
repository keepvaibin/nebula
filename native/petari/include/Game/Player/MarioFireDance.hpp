#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFireDance : public MarioState {
public:
    MarioFireDance(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    void impact();
    void impactEffect();

    Vec3f mMoveVelocity;  // the hop's horizontal velocity
    f32 mVerticalSpeed;  // the hop's speed along the gravity (below 0: rising)
    u8 mWallStart;  // started off a wall: no speed cap
    u16 mTurnLockTimer;  // after the wall kick: frames before the stick turns him again
    u8 mHopCount;  // hops so far (the second is higher)
    u8 mNeedle;  // a needle's dance (doNeedle): its own effects
};
