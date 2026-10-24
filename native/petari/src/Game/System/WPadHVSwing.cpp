#include "Game/System/WPadHVSwing.hpp"
#include "Game/System/WPad.hpp"

#ifdef TARGET_PC
#include <revolution/wpad.h>

extern "C" u32 __NebulaWPADTakeShakePresses(s32 chan, s32 device);

namespace {
    // A shake press's swing lasts as long as a flick of the remote keeps the detector above its threshold.
    const s32 cNebulaShakeSwingFrames = 6;
};  // namespace
#endif

WPadHVSwing::WPadHVSwing(const WPad* pPad, u32 channel)
    : mPad(pPad), mChannel(channel), mDistanceSwingThreshold(1.0f), mIsSwing(), mIsSwingLatched(), mSwingLatchedFrames(),
      mSwingBelowThresholdFrames(), mIsTriggerSwing(), mSwingThreshold(0.4f), mSwingDetected(), mSwingTriggered(), mSwingHoldFrames(),
      mSwingCooldownFrames()
#ifdef TARGET_PC
      ,
      mNebulaShakePresses(0), mNebulaShakeFrames(0), mIsNebulaShaking(false)
#endif
{
}

void WPadHVSwing::updateSwing() {
    TVec3f pastAccel;
    TVec3f curAccel;

    if (!mPad->getPastAcceleration(&pastAccel, 20, mChannel) || !mPad->getAcceleration(&curAccel, mChannel)) {
        mIsSwing = false;
        return;
    }

    mIsSwing = curAccel.distance(pastAccel) >= mDistanceSwingThreshold;

    if (!mIsSwing) {
        mSwingBelowThresholdFrames++;
    } else {
        mSwingBelowThresholdFrames = 0;
    }

    if (mIsSwingLatched) {
        if (mSwingBelowThresholdFrames > 6) {
            mIsSwingLatched = false;
        }
    } else if (mIsSwing) {
        mIsSwingLatched = true;
    }

    if (mIsSwingLatched) {
        mSwingLatchedFrames++;
    } else {
        mSwingLatchedFrames = 0;
    }
}

void WPadHVSwing::updateCentrifugal() {
    TVec3f pastAccel;
    TVec3f curAccel;

    if (!mPad->getPastAcceleration(&pastAccel, 20, mChannel) || !mPad->getAcceleration(&curAccel, mChannel)) {
        mSwingDetected = false;
        mSwingTriggered = false;
        mSwingHoldFrames = 0;
        mSwingCooldownFrames = 0;
    }

    float accelDeltaSum = 0.0f;
    float peakDelta = 0.0f;

    if (mPad->getEnableAccelPastCount(mChannel) >= 15) {
        for (int i = 1; i < 15; i++) {
            TVec3f prevSample;
            mPad->getPastAcceleration(&prevSample, i - 1, mChannel);

            TVec3f currSample;
            mPad->getPastAcceleration(&currSample, i, mChannel);

            accelDeltaSum += (prevSample.y - currSample.y);

            if (peakDelta < accelDeltaSum) {
                peakDelta = accelDeltaSum;
            }
        }
    }

    mSwingDetected = peakDelta > mSwingThreshold;

    if (!mSwingDetected) {
        mSwingCooldownFrames++;
    } else {
        mSwingCooldownFrames = 0;
    }

    if (mSwingTriggered) {
        if (mSwingCooldownFrames > 8) {
            mSwingTriggered = false;
        }
    } else if (mSwingDetected) {
        mSwingTriggered = true;
    }

    if (mSwingTriggered) {
        mSwingHoldFrames++;
    } else {
        mSwingHoldFrames = 0;
    }
}

#ifdef TARGET_PC
// A shake button (a controller without motion sensors, or a key) swings directly, the frame it is pressed: each press is
// a new swing (the flag drops for one frame first if a press's swing is still up), held for cNebulaShakeSwingFrames.
// Without presses the accelerometer decides, as on the console.
bool WPadHVSwing::updateNebulaShake() {
    mNebulaShakePresses += __NebulaWPADTakeShakePresses(mPad->mChannel, mChannel == WPAD_DEV_CORE ? 0 : 1);
    if (mNebulaShakeFrames == 0 && mNebulaShakePresses == 0 && !mIsNebulaShaking) {
        return false;
    }

    bool isSwing = false;
    if (mNebulaShakeFrames > 0 && !(mNebulaShakePresses > 0 && mNebulaShakeFrames < cNebulaShakeSwingFrames - 1)) {
        mNebulaShakeFrames--;
        isSwing = true;
    } else if (mNebulaShakePresses > 0 && !mIsNebulaShaking) {
        mNebulaShakePresses--;
        mNebulaShakeFrames = cNebulaShakeSwingFrames - 1;
        isSwing = true;
    } else {
        mNebulaShakeFrames = 0;
    }

    mIsNebulaShaking = isSwing;
    mIsSwing = isSwing;
    mIsSwingLatched = isSwing;
    mSwingLatchedFrames = isSwing ? mSwingLatchedFrames + 1 : 0;
    mSwingBelowThresholdFrames = isSwing ? 0 : mSwingBelowThresholdFrames + 1;
    mSwingDetected = false;
    mSwingTriggered = false;
    mSwingHoldFrames = 0;
    mIsTriggerSwing = mSwingLatchedFrames == 1;
    return true;
}
#endif

void WPadHVSwing::update() {
#ifdef TARGET_PC
    if (updateNebulaShake()) {
        return;
    }
#endif
    updateSwing();
    updateCentrifugal();

    mIsTriggerSwing = false;

    if (mSwingLatchedFrames == 1) {
        mIsTriggerSwing = true;
    } else if (mSwingHoldFrames >= 30) {
        bool isOffPulseBoundary = (mSwingHoldFrames - 30) % 15 != 0;

        if (!isOffPulseBoundary && mSwingCooldownFrames < 15) {
            mIsTriggerSwing = true;
        }
    }
}
