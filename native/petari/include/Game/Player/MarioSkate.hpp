#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioSkate : public MarioState {
public:
    MarioSkate(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice() {
        return false;
    }
    virtual bool postureCtrl(MtxPtr);

    void exitJump();

    u32 mStrideTimer;  // frames since the last stride
    u8 mExitJump;  // left by a jump (exitJump)
    bool mBackward;  // skating backward (Z turns it)
    u8 mStandStart;  // started slow: the stand animation plays out, no move
    s8 mStrideSide;  // the next stride's side (0 right, 1 left)
    u8 mStrideReady;  // the next stride may start
    u8 mStrideCount;  // strides so far (the start's counts)
    f32 mTwist;  // the twist animation's phase (-1 to 1)
    f32 mTilt;  // the side tilt, smoothed
};
