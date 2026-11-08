#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;
class HitSensor;
class Triangle;

class MarioHang : public MarioState {
public:
    MarioHang(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual bool postureCtrl(MtxPtr);

    void recordWallPolygon(const Triangle*);
    void recordHangNorm(const Vec3f&);
    void forceDrop();
    void tryClimb(bool);

    u16 mClimbStep;  // the climb's step (0 hanging)
    u16 mTimer;  // frames in the step
    u16 mSlipUpTimer;  // frames until the slip up (a jump at the hang's start)
    u16 mHangTimer;
    u8 mSlipUp;  // the slip up is due: the hang ends in it (close)
    u8 mClimbQueued;  // a climb asked during the hang's start
    u8 mLetGo;  // he lets go (pushed off, moved, or no room)
    u8 mDropQueued;  // a drop asked during the hang's start
    u8 mJustStarted;  // the hang just began (checkHang): a jump makes the slip up
    Triangle* mWallPolygon;  // the wall's polygon (a copy)
    Vec3f mLocalNormal;  // the hang normal in the polygon's space
    Vec3f mWallNormal;  // the hang normal
    HitSensor* mWallSensor;
};
