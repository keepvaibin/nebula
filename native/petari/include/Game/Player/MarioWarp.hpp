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

    Vec3f mTargetPos;  // where the warp ends
    Vec3f mStartPos;  // where the warp starts
    Vec3f mEndUp;  // the end's up (the power jump out)
    Vec3f mEndFront;  // the end's front
    u8 mInputFree;  // spin and input stay on (the pod's arg2)
    u8 mKind;  // 0 cube, 1 pod, 2 point, 3 recovery
    u32 mPointFrames;  // the point warp's frames (doPointWarp)
    LiveActor* mPod;  // the warp pod (doObjWarp)
    u16 mEnterTimer;  // frames left of the move to the start
    u16 mEnterLength;  // the move's frames
    u16 mEndAnimStarted;  // the end animation started
    Vec3f mArcAxis;  // the arc's axis (calcAxis)
    Vec3f mArcCenter;  // the arc's center
    Vec3f mArcStart;  // the start's offset from the center
    f32 mArcStartAngle;  // the arc's start angle
    f32 mArcEndAngle;  // the arc's end angle
    s32 mTimer;  // frames left of the travel
    s32 mLength;  // the travel's frames
};
