#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioStun : public MarioState {
public:
    MarioStun(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    u16 mRecovering;  // the recovery runs: a rush or A ends it
    u16 mRecoverTimer;  // frames until he can act (60, then 10 or 30)
};
