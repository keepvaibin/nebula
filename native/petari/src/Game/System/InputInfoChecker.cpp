#include "Game/System/InputInfoChecker.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"

namespace {
    static const s32 sCheckInfoInterval = 18000;
};  // namespace

InputInfoChecker::InputInfoChecker(PlayerInput* pPad) : mPad(pPad) {
    reset();
}

s32 InputInfoChecker::getBattery() const {
    return mBattery;
}

void InputInfoChecker::startCheck() {
    mBattery = mPad->mReadInfo->mDevice.battery;

    mCheckInfoFrame = ::sCheckInfoInterval;
}

void InputInfoChecker::reset() {
    mCheckInfoFrame = 0;
    mBattery = -1;
}

void InputInfoChecker::update() {
    if (mPad->mIsConnected) {
        if (mCheckInfoFrame <= 0) {
            startCheck();
        } else {
            mCheckInfoFrame--;
        }
    } else {
        reset();
    }
}
