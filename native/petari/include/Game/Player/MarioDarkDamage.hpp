#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioDarkDamage : public MarioState {
public:
    MarioDarkDamage(MarioActor*);

    virtual bool start();
    virtual bool update();
    virtual bool notice();

    u16 mTimer;  // frames left of the damage
    u16 mStep;  // update's step
};
