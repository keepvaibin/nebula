#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFlip : public MarioState {
public:
    MarioFlip(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    void setVec(const TVec3f&);

    u16 _12;
    u16 _14;
    TVec3f _18;
    f32 _24;
    f32 _28;
};
