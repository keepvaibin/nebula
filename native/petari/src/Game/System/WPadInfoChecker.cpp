#include "Game/System/WPadInfoChecker.hpp"
#include "Game/System/WPad.hpp"
#include "Game/System/WPadHolder.hpp"

namespace {
    static const s32 sCheckInfoInterval = 18000;
};  // namespace

WPadInfoChecker::WPadInfoChecker(WPad* pPad) : mPad(pPad) {
    reset();
}

s32 WPadInfoChecker::getBattery() const {
    return mBattery;
}

void WPadInfoChecker::startCheck() {
    mBattery = mPad->mReadInfo->mDevice.battery;

    mCheckInfoFrame = ::sCheckInfoInterval;
}

void WPadInfoChecker::reset() {
    mCheckInfoFrame = 0;
    mBattery = -1;
}

void WPadInfoChecker::update() {
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
