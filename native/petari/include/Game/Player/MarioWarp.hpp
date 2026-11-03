#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioWarp : public MarioState {
public:
    MarioWarp(MarioActor*);

    virtual bool start();
    virtual bool update();
    virtual bool close();

    void calcAxis();
    void updateJump();

    u8 _12;
    TVec3f _14;
    TVec3f _20;
    TVec3f _2C;
    TVec3f _38;
    u8 _44;
    u8 _45;
    u32 _48;
    LiveActor* _4C;
    bool _50;
    u16 _52;
    u16 _54;
    u16 _56;
    u16 _58;
    TVec3f _5C;
    TVec3f _68;
    TVec3f _74;
    f32 _80;
    f32 _84;
    s32 _88;
    s32 _8C;
};
