#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioStep : public MarioState {
public:
    MarioStep(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    f32 mBlendFrames;  // the step's blend frames, counted down by the walk speed
    f32 mStartSpeed;  // the walk speed at the start
};
