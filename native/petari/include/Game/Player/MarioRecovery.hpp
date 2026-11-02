#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioRecovery : public MarioState {
public:
    MarioRecovery(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    bool calcFirstVector();
    void updateJump();

    u8 _11;
    u8 _12;
    u16 _14;
    u16 _16;
    u16 _18;
    u16 _1A;
    TVec3f _1C;
    TVec3f _28;
    TVec3f _34;
    TVec3f _40;
    TVec3f _4C;
    TVec3f _58;
    TVec3f _64;
    TVec3f _70;
    f32 _7C;
    f32 _80;
    f32 _84;
    u16 _88;
};
