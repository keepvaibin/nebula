#pragma once

#include <nebula_geometry.hpp>

class PlayerInput;

class InputStick {
public:
    InputStick(const PlayerInput*);

    void update();
    bool isChanged() const;

    const PlayerInput* mPad;
    Vec2f mStick;
    f32 mSpeed;
    u32 mHold;
    u32 mTrigger;
    u32 mRelease;
    bool mIsTriggerUp;
    bool mIsTriggerDown;
    bool mIsHoldUp;
    bool mIsHoldDown;
};
