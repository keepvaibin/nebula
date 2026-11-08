#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioTeresa : public MarioState {
public:
    MarioTeresa(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice() {
        return true;
    }

    virtual bool keep() {
        return update();
    }

    void updateDropFlag();
    void resetTeresaMode();
    bool isTeresaAccel() const;
    f32 checkHeight();
    void checkAccel();
    void checkWind();
    void checkGroundReflect();
    void checkWallCeilReflect();
    void procNoControl();
    void procNearGroundControl();
    void procAirControl();
    void procDrop();
    void procControl();
    void addTeresaVerticalVelocity(f32);
    void addTeresaHorizontalVelocity(const Vec3f&);
    void doTeresaReflection(const Vec3f&, bool);

    Vec3f mPrevJumpVec;  // the jump vector at the frame's start
    f32 mHeight;  // the height above the ground (checkHeight)
    f32 mWaitHeight;  // the float height (mTeresaWaitHeight)
    Vec3f mWind;  // the wind, smoothed
    Vec3f mVelocity;  // the Boo's own velocity
    u16 mReflectTimer;  // frames since a reflection (0: none)
    u16 mAccelTimer;  // frames left of the accel
    u16 mWallReflectTimer;  // frames left of a wall reflection
    u16 mPushTimer;  // frames of the push after a press
    u8 mDropping;  // falling straight with no stick (updateDropFlag)
};
