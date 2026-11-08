#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioWait : public MarioState {
public:
    MarioWait(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    bool checkStart();

    u16 mKind;  // 0 the idle wait, 1 the battle wait (NonSleepCube)
    u16 mStep;  // the idle wait's step
    u16 mTimer;  // frames in the wait (1800: the special wait)
};
