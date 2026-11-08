#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioRabbit : public MarioState {
public:
    MarioRabbit(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice() {
        return true;
    }

    void hop();
    void forceJump();
    void impact();

    f32 mVerticalSpeed;  // the hop's speed along the gravity (below 0: rising)
    Vec3f mMoveVelocity;  // the hop's horizontal velocity
    u8 mWallStart;  // started off a wall: the move speed cap waits
    u8 mForcedJump;  // a forced jump (forceJump): procJump drives the air
    u8 mImpacted;  // a hard landing (impact): the jump animation restarts
    u8 mHighJump;  // the high jump is charging or on
    u16 mTurnLockTimer;  // after the wall kick: frames before the stick turns him again
    Mtx mBodyTiltMtx;  // the hip and spine's tilt
    Vec3f mBodyFront;  // the body's front, lagging the front
    u8 mJumpSide;  // the jump animation's side (A 0, B 1), alternating
    u8 mJumpBuffer;  // frames a jump press waits for the landing
    u8 mBounceSound;  // the bounce sound is due at the landing
};
