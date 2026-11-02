#include "Game/System/GamePadButton.hpp"
#include "Game/System/GamePad.hpp"
#include "Game/System/GamePadHolder.hpp"

GamePadButton::GamePadButton(const GamePad* pPad) : mPad(pPad), mHold(), mTrigger(), mRelease(), mRepeat(), mDelaySec(1.0f / 2.4f), mPulseSec(1.0f / 6.0f) {
    mPad->mReadInfo->setButtonRepeat(mDelaySec, mPulseSec);
}

void GamePadButton::update() {
    GamePadSample* pSample = mPad->getSample(0);

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

bool GamePadButton::testButtonUp() const {
    return (mHold & NEBULA_BUTTON_UP) != 0;
}

bool GamePadButton::testButtonDown() const {
    return (mHold & NEBULA_BUTTON_DOWN) != 0;
}

bool GamePadButton::testButtonLeft() const {
    return (mHold & NEBULA_BUTTON_LEFT) != 0;
}

bool GamePadButton::testButtonRight() const {
    return (mHold & NEBULA_BUTTON_RIGHT) != 0;
}

bool GamePadButton::testButtonA() const {
    return (mHold & NEBULA_BUTTON_A) != 0;
}

bool GamePadButton::testButtonB() const {
    return (mHold & NEBULA_BUTTON_B) != 0;
}

bool GamePadButton::testButtonMinus() const {
    return (mHold & NEBULA_BUTTON_MINUS) != 0;
}

bool GamePadButton::testButtonPlus() const {
    return (mHold & NEBULA_BUTTON_PLUS) != 0;
}

bool GamePadButton::testButton1() const {
    return (mHold & NEBULA_BUTTON_1) != 0;
}

bool GamePadButton::testButton2() const {
    return (mHold & NEBULA_BUTTON_2) != 0;
}

bool GamePadButton::testButtonC() const {
    return (mHold & NEBULA_BUTTON_C) != 0;
}

bool GamePadButton::testButtonZ() const {
    return (mHold & NEBULA_BUTTON_Z) != 0;
}

bool GamePadButton::testTriggerUp() const {
    return (mTrigger & NEBULA_BUTTON_UP) != 0;
}

bool GamePadButton::testTriggerDown() const {
    return (mTrigger & NEBULA_BUTTON_DOWN) != 0;
}

bool GamePadButton::testTriggerLeft() const {
    return (mTrigger & NEBULA_BUTTON_LEFT) != 0;
}

bool GamePadButton::testTriggerRight() const {
    return (mTrigger & NEBULA_BUTTON_RIGHT) != 0;
}

bool GamePadButton::testTriggerA() const {
    return (mTrigger & NEBULA_BUTTON_A) != 0;
}

bool GamePadButton::testTriggerB() const {
    return (mTrigger & NEBULA_BUTTON_B) != 0;
}

bool GamePadButton::testTriggerMinus() const {
    return (mTrigger & NEBULA_BUTTON_MINUS) != 0;
}

bool GamePadButton::testTriggerHome() const {
    return (mTrigger & NEBULA_BUTTON_HOME) != 0;
}

bool GamePadButton::testTriggerPlus() const {
    return (mTrigger & NEBULA_BUTTON_PLUS) != 0;
}

bool GamePadButton::testTrigger1() const {
    return (mTrigger & NEBULA_BUTTON_1) != 0;
}

bool GamePadButton::testTrigger2() const {
    return (mTrigger & NEBULA_BUTTON_2) != 0;
}

bool GamePadButton::testTriggerC() const {
    return (mTrigger & NEBULA_BUTTON_C) != 0;
}

bool GamePadButton::testTriggerZ() const {
    return (mTrigger & NEBULA_BUTTON_Z) != 0;
}

bool GamePadButton::testReleaseZ() const {
    return (mRelease & NEBULA_BUTTON_Z) != 0;
}

bool GamePadButton::isChangeAnyState() const {
    return mTrigger != 0 || mRelease != 0;
}
