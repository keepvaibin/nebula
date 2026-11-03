#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFireDance : public MarioState {
public:
    MarioFireDance(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    void impact();
    void impactEffect();

    TVec3f _14;
    f32 _20;
    u8 _24;
    u16 _26;
    u8 _28;
    u8 _29;
};
