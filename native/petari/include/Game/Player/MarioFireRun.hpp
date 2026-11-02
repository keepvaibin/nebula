#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFireRun : public MarioState {
public:
    MarioFireRun(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    bool move();

    u16 _12;
    u16 _14;
    f32 _18;
};
