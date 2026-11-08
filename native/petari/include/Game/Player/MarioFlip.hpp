#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFlip : public MarioState {
public:
    MarioFlip(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    void setVec(const Vec3f&);

    u16 mTimer;  // frames since the flip
    u16 mStep;  // update's step (2 on: the spin punch may start)
    Vec3f mVelocity;  // the slide's velocity
    f32 mYAngle;  // the spin's angle offset
    f32 mYSpeed;  // the spin's angular speed
};
