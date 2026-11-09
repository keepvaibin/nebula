#pragma once

#include "Game/Player/MarioState.hpp"
#include <nebula_geometry.hpp>

class MarioActor;
class AreaObj;
class GameTexture;

class MarioFoo : public MarioState {
public:
    MarioFoo(MarioActor*);

    virtual f32 getStickY() const;
    virtual void init();
    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual void hitWall(const Vec3f&, HitSensor*);
    virtual bool passRing(const HitSensor*);
    virtual f32 getBlurOffset() const;
    virtual void draw3D() const;
    virtual const Vec3f& getGravityVec() const;

    void setStartSpeed(f32 speed) {
        mSpeed = speed;
        mBrakeTimer = 20;
    }

    void jet();
    void updateTilt();
    void spin();
    f32 calcRingAcc();

    u8 mBraked;  // Z or a wall ended the flight (update returns false)
    u8 mJetHeldFromStart;  // A held since the start: no brake until it is let go
    u32 mFrameCount;  // frames since start
    u16 mPitchTimer;  // frames the stick pitches him; nothing sets it
    u16 mVerticalTimer;  // frames of the vertical blend (50 down); nothing sets it
    u16 mIdleTimer;  // frames with no input at low speed (to 120): the neutral pose's blend
    u16 mSphereHitTimer;  // frames until the sphere's edge can hit again
    f32 mStickPitch;  // the stick's y, smoothed (mSwimRotXIne)
    f32 mStickRoll;  // the stick's x, smoothed (mSwimRotZIne)
    f32 mSpeed;  // the flight's speed
    f32 mPitch;  // the pitch off the up (radians)
    Vec3f mFrontVec;  // the flight's front, kept square to the up
    Vec3f mUpVec;  // against the gravity
    AreaObj* mCelestialSphere;  // the CelestrialSphere area he flies in
    u16 mSpinTimer;  // frames of the spin (the spin sensor's on)
    u16 mRingBoostTimer;  // frames of the ring's boost
    u16 mRingChargeTimer;  // frames of the ring dash's charge
    f32 mBlurOffset;  // the screen blur (getBlurOffset)
    u8 mEndKind;  // how the flight ended: 0-2 a wall by its angle, 3 braked (close)
    u16 mRingBoostLength;  // the ring's boost frames
    f32 mRingBoostScale;  // the ring's speed scale
    u16 mPushTimer;  // frames of mPushVelocity; nothing sets it
    Vec3f mPushVelocity;  // a push added each frame; nothing sets it
    f32 mSideTilt;  // the upper body's side tilt
    f32 mFrontTilt;  // the upper body's front tilt
    Mtx34f mTiltMtx;  // the upper body's tilt
    u16 mBrakeTimer;  // frames of the brake (A); 8 or more boost after it (to 20)
    u8 mBraking;  // braking
    static const u32 cTrailNum = 64;  // the trail's points, one taken each update
    Vec3f mTrailPos[cTrailNum];  // the trail's points (a ring)
    Vec3f mTrailSide[cTrailNum];  // each trail point's hand-to-hand direction
    u32 mTrailLength;  // points in the trail
    u32 mTrailIndex;  // the next trail point
    GameTexture* mTrailTexture;  // FooLine.bti
};
