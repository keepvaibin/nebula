#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class GamePad;

class GamePadStick {
public:
    GamePadStick(const GamePad*);

    void update();
    bool isChanged() const;

    const GamePad* mPad;
    TVec2f mStick;
    f32 mSpeed;
    u32 mHold;
    u32 mTrigger;
    u32 mRelease;
    bool mIsTriggerUp;
    bool mIsTriggerDown;
    bool mIsHoldUp;
    bool mIsHoldDown;
};
