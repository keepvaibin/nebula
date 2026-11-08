#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFireRun : public MarioState {
public:
    MarioFireRun(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    bool move();

    u16 mTimer;  // frames left of the step
    u16 mStep;  // update's step
    f32 mVerticalSpeed;  // the hop's speed along the gravity
};
