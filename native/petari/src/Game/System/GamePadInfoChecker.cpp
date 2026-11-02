#include "Game/System/GamePadInfoChecker.hpp"
#include "Game/System/GamePad.hpp"
#include "Game/System/GamePadHolder.hpp"

namespace {
    static const s32 sCheckInfoInterval = 18000;
};  // namespace

GamePadInfoChecker::GamePadInfoChecker(GamePad* pPad) : mPad(pPad) {
    reset();
}

s32 GamePadInfoChecker::getBattery() const {
    return mBattery;
}

void GamePadInfoChecker::startCheck() {
    mBattery = mPad->mReadInfo->mDevice.battery;

    mCheckInfoFrame = ::sCheckInfoInterval;
}

void GamePadInfoChecker::reset() {
    mCheckInfoFrame = 0;
    mBattery = -1;
}

void GamePadInfoChecker::update() {
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
