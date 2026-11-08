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
    f32 mVerticalSpeed;  // the hop along the gravity
    u16 mThawing;  // the thaw's last timer runs
    u16 mFreezeTimer;
    u16 mAfterTimer;  // frames after it: no new damage
};
