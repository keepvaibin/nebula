#pragma once

class PlayerInput;

// A hand's spin: each press the host counted starts a swing that lasts a flick's time.
class InputSwing {
public:
    InputSwing(const PlayerInput*, u32);

    void update();

    const PlayerInput* mPad;
    s32 mHand;
    bool mIsSwing;
    bool mIsTriggerSwing;
    s32 mPresses;  // presses not yet swung
    s32 mFrames;   // frames left of the swing
};
