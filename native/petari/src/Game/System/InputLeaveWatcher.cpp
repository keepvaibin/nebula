#include "Game/System/InputLeaveWatcher.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/InputTilt.hpp"
#include "Game/System/InputButtons.hpp"
#include "Game/System/InputPointer.hpp"
#include "Game/System/InputStick.hpp"

namespace {
    static bool sIsCheckPointerMoved = false;
    static bool sIsCheckCoreAccel = true;
    static bool sIsCheckSubAccel = true;
    static bool sIsCheckButton = true;
    static bool sIsCheckStick = true;
    static const s32 sLeaveLongTime = 3600;
};  // namespace

InputLeaveWatcher::InputLeaveWatcher(PlayerInput* pPad) : mPad(pPad), mStep(), mIsSuspend() {
}

void InputLeaveWatcher::update() {
    if (mIsSuspend) {
        return;
    }

    if (mPad->mPointer->mIsPointerMoved && ::sIsCheckPointerMoved || !mPad->mMainTilt->isStationary() && ::sIsCheckCoreAccel ||
        !mPad->mSubTilt->isStationary() && ::sIsCheckSubAccel || mPad->mButton->isChangeAnyState() && ::sIsCheckButton ||
        mPad->mStick->isChanged() && ::sIsCheckStick) {
        mStep = 0;
    } else if (mStep < ::sLeaveLongTime) {
        mStep++;
    }
}

void InputLeaveWatcher::start() {
    mIsSuspend = false;
}

void InputLeaveWatcher::stop() {
    mIsSuspend = true;
}

void InputLeaveWatcher::restart() {
    mStep = 0;
    mIsSuspend = false;
}
