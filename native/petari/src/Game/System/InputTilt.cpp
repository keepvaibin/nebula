#include "Game/System/InputTilt.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"

InputTilt::InputTilt(const PlayerInput* pPad, u32 hand) : mPad(pPad), mHand(hand), mTilt(0.0f, 0.0f, 0.0f), mValid(false) {
}

bool InputTilt::getAcceleration(TVec3f* pOut) const {
    if (!mValid) {
        pOut->zero();
        return false;
    }

    pOut->set(mTilt);
    return true;
}

void InputTilt::update() {
    const PlayerInputSample* const pSample = mPad->getSample(0);
    if (pSample == nullptr || (mHand != INPUT_HAND_MAIN && !MR::hasMoveStick(pSample))) {
        return;
    }

    mTilt = mHand == INPUT_HAND_MAIN ? pSample->mAccel : pSample->mSubTilt;
    mValid = true;
}
