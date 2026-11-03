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

    u16 _12;
    u16 _14;
    u16 _16;
};
