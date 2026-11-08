#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioWall : public MarioState {
public:
    MarioWall(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    void initTriangleJump();
    u32 isCancel();
    bool startJump();
    bool startBackJump(u32);

    u32 mStickTimer;  // frames on the wall
    u32 mSlideTimer;  // frames of the slide down (0: not sliding)
    u8 mCancel;  // the wall stick is cancelled (isCancel)
    u8 mOnBinderFloor;  // the binder hit a floor last frame
    u8 mIceWall;  // an ice wall was made at the start
    f32 mSlideSpeed;  // the slide's speed, smoothed
    Vec3f mWallVec;  // the wall's normal, or the jump's velocity (the cancel test, the effects)
    Vec3f mTriangleJumpDir;  // the jump's horizontal direction before the wall (initTriangleJump)
};
