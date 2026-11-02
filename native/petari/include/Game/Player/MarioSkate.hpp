#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioSkate : public MarioState {
public:
    MarioSkate(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice() {
        return false;
    }
    virtual bool postureCtrl(MtxPtr);

    void exitJump();

    u32 _14;
    u8 _18;
    bool _19;
    u8 _1A;
    s8 _1B;
    u8 _1C;
    u8 _1D;
    f32 _20;
    f32 _24;
};
