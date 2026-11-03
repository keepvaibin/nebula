#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioRabbit : public MarioState {
public:
    MarioRabbit(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice() {
        return true;
    }

    void hop();
    void forceJump();
    void impact();

    f32 _14;
    TVec3f _18;
    u8 _24;
    u8 _25;
    u8 _26;
    u8 _27;
    u8 _28;
    u16 _2A;
    Mtx _2C;
    TVec3f _5C;
    u8 _68;
    u8 _69;
    u8 _6A;
};
