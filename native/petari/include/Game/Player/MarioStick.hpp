#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioStick : public MarioState {
public:
    MarioStick(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual bool postureCtrl(MtxPtr);

    bool startJump();
    bool setStickSensor(const HitSensor*);

    Vec3f _14;
    Vec3f _20;
    Vec3f _2C;
    Vec3f _38;
    f32 _44;
    Triangle* _48;
    u32 _4C;
    u8 _50;
    u8 _51;
    u8 _52;
    u8 _53;
    Vec3f _54;
    Vec3f _60;
    Vec3f _6C;
    Vec3f _78;
};
