#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFlow : public MarioState {
public:
    MarioFlow(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    u16 mTimer;  // frames in the step
    u16 mStep;  // update's step
    Vec3f mVelocity;  // the flow's velocity
};
