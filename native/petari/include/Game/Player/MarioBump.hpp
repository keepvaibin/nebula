#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioBump : public MarioState {
public:
    MarioBump(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    u16 mBlendTimer;  // frames left of the blend
    f32 mStartSpeed;  // the walk speed at the start
};
