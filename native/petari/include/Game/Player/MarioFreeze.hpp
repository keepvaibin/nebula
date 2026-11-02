#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFreeze : public MarioState {
public:
    MarioFreeze(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    bool mIsFrozen;
    f32 _14;
    u16 _18;
    u16 mFreezeTimer;
    u16 _1C;
};
