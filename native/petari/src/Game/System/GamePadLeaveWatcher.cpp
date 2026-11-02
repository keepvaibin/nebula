#include "Game/System/GamePadLeaveWatcher.hpp"
#include "Game/System/GamePad.hpp"
#include "Game/System/GamePadAcceleration.hpp"
#include "Game/System/GamePadButton.hpp"
#include "Game/System/GamePadPointer.hpp"
#include "Game/System/GamePadStick.hpp"

namespace {
    static bool sIsCheckPointerMoved = false;
    static bool sIsCheckCoreAccel = true;
    static bool sIsCheckSubAccel = true;
    static bool sIsCheckButton = true;
    static bool sIsCheckStick = true;
    static const s32 sLeaveLongTime = 3600;
};  // namespace

GamePadLeaveWatcher::GamePadLeaveWatcher(GamePad* pPad) : mPad(pPad), mStep(), mIsSuspend() {
}

void GamePadLeaveWatcher::update() {
    if (mIsSuspend) {
        return;
    }

    if (mPad->mPointer->mIsPointerMoved && ::sIsCheckPointerMoved || !mPad->mCorePadAccel->isStationary() && ::sIsCheckCoreAccel ||
        !mPad->mSubPadAccel->isStationary() && ::sIsCheckSubAccel || mPad->mButton->isChangeAnyState() && ::sIsCheckButton ||
        mPad->mStick->isChanged() && ::sIsCheckStick) {
        mStep = 0;
    } else if (mStep < ::sLeaveLongTime) {
        mStep++;
    }
}

void GamePadLeaveWatcher::start() {
    mIsSuspend = false;
}

void GamePadLeaveWatcher::stop() {
    mIsSuspend = true;
}

void GamePadLeaveWatcher::restart() {
    mStep = 0;
    mIsSuspend = false;
}
