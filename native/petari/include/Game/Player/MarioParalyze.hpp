#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioParalyze : public MarioState {
public:
    MarioParalyze(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    u16 mRecovering;  // the recovery runs: a rush or A ends it
    u16 mTimer;
    u16 mAfterTimer;  // frames after it: no new damage
    bool mNotDecLife;
};
