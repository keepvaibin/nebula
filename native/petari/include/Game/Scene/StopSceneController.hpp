#pragma once

#include "Game/NameObj/NameObj.hpp"

class StopSceneController : public NameObj {
public:
    StopSceneController();

    virtual void movement() override;

    // Counts the delayed requests down; held while the scene's objects are suspended.
    void movementDelayRequests();
    void requestStopScene(s32);
    void requestStopSceneDelay(s32, s32);
    bool isSceneStopped() const;

    // Requests due on the same count share one entry: their stops land together, and the longest wins.
    struct DelayRequest {
        s32 mFrame;
        s32 mDelay;
    };

    DelayRequest* mDelayRequests;
    s32 mDelayRequestNum;
    s32 mDelayRequestMax;
    s32 mFrame;
};
