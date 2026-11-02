#pragma once

#include "Game/NameObj/NameObj.hpp"

class NameObjGroup;

class StopSceneDelayRequest : public NameObj {
public:
    /// @brief Creates a new `StopSceneDelayRequest`.
    StopSceneDelayRequest();

    virtual void movement();

    s32 mFrame;
    s32 mDelay;
};

class StopSceneController : public NameObj {
public:
    /// @brief Creates a new `StopSceneController`.
    StopSceneController();

    virtual void movement();

    void requestStopScene(s32);
    void requestStopSceneDelay(s32, s32);
    bool isSceneStopped() const;

    NameObjGroup* mDelayRequestArray;
    s32 mFrame;
};
