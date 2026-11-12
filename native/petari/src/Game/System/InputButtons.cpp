#include "Game/System/InputButtons.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/LumaTime.hpp"

InputButtons::InputButtons(const PlayerInput* pPad)
    : mPad(pPad), mHold(), mTrigger(), mRelease(), mCarriedTrigger(0), mCarriedRelease(0) {
}

void InputButtons::update() {
    const PlayerInputSample* const pSample = mPad->getSample();
    if (pSample == nullptr) {
        return;
    }

    // A press counts once the button was up last frame.
    mTrigger = pSample->mTrigger & ~mHold;
    mHold = pSample->mHold;
    mRelease = pSample->mRelease;
    if (luma::time::isStepWithheld()) {
        mCarriedTrigger |= mTrigger;
        mCarriedRelease |= mRelease;
    } else if ((mCarriedTrigger | mCarriedRelease) != 0) {
        mTrigger |= mCarriedTrigger;
        mRelease |= mCarriedRelease;
        mCarriedTrigger = mCarriedRelease = 0;
    }
}

bool InputButtons::testButtonUp() const {
    return (mHold & NEBULA_BUTTON_UP) != 0;
}

bool InputButtons::testButtonDown() const {
    return (mHold & NEBULA_BUTTON_DOWN) != 0;
}

bool InputButtons::testButtonLeft() const {
    return (mHold & NEBULA_BUTTON_LEFT) != 0;
}

bool InputButtons::testButtonRight() const {
    return (mHold & NEBULA_BUTTON_RIGHT) != 0;
}

bool InputButtons::testButtonA() const {
    return (mHold & NEBULA_BUTTON_A) != 0;
}

bool InputButtons::testButtonB() const {
    return (mHold & NEBULA_BUTTON_B) != 0;
}

bool InputButtons::testButtonMinus() const {
    return (mHold & NEBULA_BUTTON_MINUS) != 0;
}

bool InputButtons::testButtonPlus() const {
    return (mHold & NEBULA_BUTTON_PLUS) != 0;
}

bool InputButtons::testButton1() const {
    return (mHold & NEBULA_BUTTON_1) != 0;
}

bool InputButtons::testButton2() const {
    return (mHold & NEBULA_BUTTON_2) != 0;
}

bool InputButtons::testButtonC() const {
    return (mHold & NEBULA_BUTTON_C) != 0;
}

bool InputButtons::testButtonZ() const {
    return (mHold & NEBULA_BUTTON_Z) != 0;
}

bool InputButtons::testTriggerUp() const {
    return (mTrigger & NEBULA_BUTTON_UP) != 0;
}

bool InputButtons::testTriggerDown() const {
    return (mTrigger & NEBULA_BUTTON_DOWN) != 0;
}

bool InputButtons::testTriggerLeft() const {
    return (mTrigger & NEBULA_BUTTON_LEFT) != 0;
}

bool InputButtons::testTriggerRight() const {
    return (mTrigger & NEBULA_BUTTON_RIGHT) != 0;
}

bool InputButtons::testTriggerA() const {
    return (mTrigger & NEBULA_BUTTON_A) != 0;
}

bool InputButtons::testTriggerB() const {
    return (mTrigger & NEBULA_BUTTON_B) != 0;
}

bool InputButtons::testTriggerMinus() const {
    return (mTrigger & NEBULA_BUTTON_MINUS) != 0;
}

bool InputButtons::testTriggerHome() const {
    return (mTrigger & NEBULA_BUTTON_HOME) != 0;
}

bool InputButtons::testTriggerPlus() const {
    return (mTrigger & NEBULA_BUTTON_PLUS) != 0;
}

bool InputButtons::testTrigger1() const {
    return (mTrigger & NEBULA_BUTTON_1) != 0;
}

bool InputButtons::testTrigger2() const {
    return (mTrigger & NEBULA_BUTTON_2) != 0;
}

bool InputButtons::testTriggerC() const {
    return (mTrigger & NEBULA_BUTTON_C) != 0;
}

bool InputButtons::testTriggerZ() const {
    return (mTrigger & NEBULA_BUTTON_Z) != 0;
}

bool InputButtons::testReleaseZ() const {
    return (mRelease & NEBULA_BUTTON_Z) != 0;
}

