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

    u8 mPullBack;  // a PullBackCylinder sent him (MarioCollision)
    u16 mTurnTimer;  // frames the front turns to mJumpFront
    u16 mTimer;  // frames left of the pull (the sound's volume)
    u16 mJumpLength;  // the jump's frames (200 and up)
    u16 mStep;  // update's step
    Vec3f mJumpFront;  // the last move's horizontal part
    Vec3f mJumpVertical;  // the last move's vertical part
    Vec3f mTargetPos;  // where he is taken: the last safe point
    Vec3f mStartPos;  // where the pull started
    Vec3f mPullBackCenter;  // the cylinder's center
    Vec3f mPullBackUp;  // the cylinder's up
    Vec3f mArcAxis;  // the jump arc's axis
    Vec3f mArcDir;  // the start's direction from the target
    f32 mArcAngle;  // the arc's angle so far
    f32 mArcStep;  // the arc's angle per frame
    f32 mArcRadius;  // the arc's radius
    u16 mJumpTimer;  // frames left of the jump
};
