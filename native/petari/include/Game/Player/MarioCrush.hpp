#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioCrush : public MarioState {
public:
    MarioCrush(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    u16 mRecovering;  // the recovery runs: a rush or A ends it
    u16 mTimer;
};
