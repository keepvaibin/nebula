#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class PlayerInput;

class InputStick {
public:
    InputStick(const PlayerInput*);

    void update();
    bool isChanged() const;

    const PlayerInput* mPad;
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
