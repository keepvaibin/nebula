#include "Game/System/InputSwing.hpp"
#include "Game/System/PlayerInput.hpp"

#include "Game/System/PlayerInputs.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
    const s32 cSwingFrames = 6;
};  // namespace

InputSwing::InputSwing(const PlayerInput* pPad, u32 hand)
    : mPad(pPad), mHand(hand), mIsSwing(false), mIsTriggerSwing(false), mPresses(0), mFrames(0) {
}

// A press while a swing is up ends it, so the new one starts a frame later as its own.
void InputSwing::update() {
    mPresses += mPad->mReadInfo->takeSpinPresses(mHand == INPUT_HAND_MAIN ? 0 : 1);

    bool isSwing = false;
    bool isStart = false;
    if (mFrames > 0 && !(mPresses > 0 && mFrames < cSwingFrames - 1)) {
        mFrames--;
        isSwing = true;
    } else if (mPresses > 0 && !mIsSwing) {
        mPresses--;
        mFrames = cSwingFrames - 1;
        isSwing = true;
        isStart = true;
    } else {
        mFrames = 0;
    }

    mIsSwing = isSwing;
    mIsTriggerSwing = isStart;
}
