#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Screen/CaptureScreenDirector.hpp"

// Holds a captured frame over the screen while the camera jumps.
class CameraCover : public NameObj {
public:
    CameraCover(const char*);

    virtual void movement();
    virtual void draw() const;

    void cover(u32 frames);

    CaptureScreenActor* mActor;
    s32 mCoverFramesLeft;
    bool mIsCoverRequested;
    u32 mCoverFrames;
};
