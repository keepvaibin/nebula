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

    u16 _12;
    u16 _14;
    TVec3f _18;
};
