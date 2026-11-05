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
    Vec3f _1C;
    Vec3f _28;
    Vec3f _34;
    Vec3f _40;
    Vec3f _4C;
    Vec3f _58;
    Vec3f _64;
    Vec3f _70;
    f32 _7C;
    f32 _80;
    f32 _84;
    u16 _88;
};
