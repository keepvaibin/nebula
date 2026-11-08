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

    Vec3f mStickPos;  // his point on the flower
    Vec3f mStickDir;  // the flower center's direction to him
    Vec3f mFlowerUp;  // the flower's up (the stick jump's direction)
    Vec3f mFlowerPos;  // the flower sensor's center
    f32 mFlowerRadius;  // the flower sensor's radius
    Vec3f mPoseFront;  // the pose's front (postureCtrl)
    Vec3f mPoseUp;  // the pose's up
};
