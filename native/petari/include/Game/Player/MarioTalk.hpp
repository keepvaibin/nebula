#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioTalk : public MarioState {
public:
    MarioTalk(MarioActor*);

    virtual bool update();
    virtual bool notice();
    virtual bool close();
    virtual bool start();

    const LiveActor* mPartner;  // the actor he talks to
    bool mIsUpdate;
};
