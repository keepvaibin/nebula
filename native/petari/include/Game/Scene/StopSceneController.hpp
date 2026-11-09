#pragma once

#include "Game/NameObj/NameObj.hpp"

class StopSceneController : public NameObj {
public:
    StopSceneController();

    virtual void movement();

    // Counts the delayed requests down; held while the scene's objects are suspended.
    void movementDelayRequests();
    void requestStopScene(s32);
    void requestStopSceneDelay(s32, s32);
    bool isSceneStopped() const;

    struct DelayRequest {
        s32 mFrame;
        s32 mDelay;  // 0 when free
    };

    DelayRequest mDelayRequests[16];
    s32 mFrame;
};
