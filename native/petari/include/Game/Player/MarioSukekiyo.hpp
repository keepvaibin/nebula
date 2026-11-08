#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioSukekiyo : public MarioState {
public:
    MarioSukekiyo(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual bool postureCtrl(MtxPtr);

    Vec3f mPoseUp;  // the pose's up (the ground's normal)
    Vec3f mPoseSide;  // the pose's side
    u8 mEscaping;  // getting out (a rush or A)
};

class MarioBury : public MarioSukekiyo {
public:
    MarioBury(MarioActor*);
};
