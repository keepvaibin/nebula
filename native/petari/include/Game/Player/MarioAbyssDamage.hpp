#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioAbyssDamage : public MarioState {
public:
    MarioAbyssDamage(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    u16 mTimer;  // frames left before the game over (120)
    u16 mStep;  // update's step
    Vec3f mMove;  // a move added each frame; nothing sets it
};
