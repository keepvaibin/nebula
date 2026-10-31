#include "Game/System/WPadButton.hpp"
#include "Game/System/WPad.hpp"
#include "Game/System/WPadHolder.hpp"

WPadButton::WPadButton(const WPad* pPad) : mPad(pPad), mHold(), mTrigger(), mRelease(), mRepeat(), mDelaySec(1.0f / 2.4f), mPulseSec(1.0f / 6.0f) {
    mPad->mReadInfo->setButtonRepeat(mDelaySec, mPulseSec);
}

void WPadButton::update() {
    WPadSample* pSample = mPad->getSample(0);

    if (pSample == nullptr) {
        return;
    }

    if (mHold != 0) {
        mTrigger = pSample->mTrigger & ~mHold;
    } else {
        mTrigger = pSample->mTrigger;
    }

    mHold = pSample->mHold;
    mRelease = pSample->mRelease;
    mRepeat = mTrigger;

    if (pSample->mRepeat) {
        mRepeat |= mHold;
    }
}

bool WPadButton::testButtonUp() const {
    return (mHold & NEBULA_BUTTON_UP) != 0;
}

bool WPadButton::testButtonDown() const {
    return (mHold & NEBULA_BUTTON_DOWN) != 0;
}

bool WPadButton::testButtonLeft() const {
    return (mHold & NEBULA_BUTTON_LEFT) != 0;
}

bool WPadButton::testButtonRight() const {
    return (mHold & NEBULA_BUTTON_RIGHT) != 0;
}

bool WPadButton::testButtonA() const {
    return (mHold & NEBULA_BUTTON_A) != 0;
}

bool WPadButton::testButtonB() const {
    return (mHold & NEBULA_BUTTON_B) != 0;
}

bool WPadButton::testButtonMinus() const {
    return (mHold & NEBULA_BUTTON_MINUS) != 0;
}

bool WPadButton::testButtonPlus() const {
    return (mHold & NEBULA_BUTTON_PLUS) != 0;
}

bool WPadButton::testButton1() const {
    return (mHold & NEBULA_BUTTON_1) != 0;
}

bool WPadButton::testButton2() const {
    return (mHold & NEBULA_BUTTON_2) != 0;
}

bool WPadButton::testButtonC() const {
    return (mHold & NEBULA_BUTTON_C) != 0;
}

bool WPadButton::testButtonZ() const {
    return (mHold & NEBULA_BUTTON_Z) != 0;
}

bool WPadButton::testTriggerUp() const {
    return (mTrigger & NEBULA_BUTTON_UP) != 0;
}

bool WPadButton::testTriggerDown() const {
    return (mTrigger & NEBULA_BUTTON_DOWN) != 0;
}

bool WPadButton::testTriggerLeft() const {
    return (mTrigger & NEBULA_BUTTON_LEFT) != 0;
}

bool WPadButton::testTriggerRight() const {
    return (mTrigger & NEBULA_BUTTON_RIGHT) != 0;
}

bool WPadButton::testTriggerA() const {
    return (mTrigger & NEBULA_BUTTON_A) != 0;
}

bool WPadButton::testTriggerB() const {
    return (mTrigger & NEBULA_BUTTON_B) != 0;
}

bool WPadButton::testTriggerMinus() const {
    return (mTrigger & NEBULA_BUTTON_MINUS) != 0;
}

bool WPadButton::testTriggerHome() const {
    return (mTrigger & NEBULA_BUTTON_HOME) != 0;
}

bool WPadButton::testTriggerPlus() const {
    return (mTrigger & NEBULA_BUTTON_PLUS) != 0;
}

bool WPadButton::testTrigger1() const {
    return (mTrigger & NEBULA_BUTTON_1) != 0;
}

bool WPadButton::testTrigger2() const {
    return (mTrigger & NEBULA_BUTTON_2) != 0;
}

bool WPadButton::testTriggerC() const {
    return (mTrigger & NEBULA_BUTTON_C) != 0;
}

bool WPadButton::testTriggerZ() const {
    return (mTrigger & NEBULA_BUTTON_Z) != 0;
}

bool WPadButton::testReleaseZ() const {
    return (mRelease & NEBULA_BUTTON_Z) != 0;
}

bool WPadButton::isChangeAnyState() const {
    return mTrigger != 0 || mRelease != 0;
}
