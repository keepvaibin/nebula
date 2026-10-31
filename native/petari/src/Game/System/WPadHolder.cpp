#include "Game/System/WPadHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/WPad.hpp"
#include "Game/System/WPadPointer.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <math.h>
#include <revolution/wpad.h>

#define WPAD_SAMPLE_MAX 120

namespace {
    // Button repeat in 200ths of a second of its setting, counted one per sample: past the longest pulse the count
    // wraps; without a pulse a held button never repeats.
    const u16 cRepeatTimeMax = 20000;
    const u16 cNoRepeatDelay = 40000;
    // The largest acceleration the remote's and the Nunchuk's sensors read, in g.
    const f32 cAccelMax = 3.4f;
    const f32 cSubAccelMax = 2.1f;

    NebulaInputSample sSamples[WPAD_SAMPLE_MAX];

    // Activity that keeps the screen from dimming, as the console's controller layer judged it (WPADiCheckContInputs'
    // IsControllerDataChanged): a button at once; the accelerometer moving more than 12 counts (of about 104 per g)
    // or the Nunchuk stick more than 2 counts (of 128) once the change has held for four samples.
    const f32 cActivityAccel = 12.0f / 104.0f;
    const f32 cActivityStick = 2.0f / 128.0f;
    const u16 cActivitySettle = 3;

    bool exceeds(const f32* pA, const f32* pB, s32 count, f32 limit) {
        for (s32 i = 0; i < count; i++) {
            const f32 diff = pA[i] - pB[i];
            if (diff > limit || diff < -limit) {
                return true;
            }
        }

        return false;
    }

    f32 clampAccel(f32 value, f32 limit) {
        return value < -limit ? -limit : value > limit ? limit : value;
    }
};  // namespace

WPadReadDataInfo::WPadReadDataInfo()
    : mStatusArray(), mValidStatusCount(), mDevice(), mState(), mResetRequested(true), mStateSubPad(false),
      mSubResetRequested(true), mShakeHeld(0), mRepeatTime(0), mRepeatNext(cNoRepeatDelay), mRepeatDelay(cNoRepeatDelay),
      mRepeatPulse(0), mAccelPlayRadius(0.0f), mAccelSensitivity(1.0f), mActivitySample(), mAccelSettle(0), mStickSettle(0) {
    mStatusArray = new WPadSample[WPAD_SAMPLE_MAX];
    MR::zeroMemory(mStatusArray, sizeof(WPadSample) * WPAD_SAMPLE_MAX);
    mShakePresses[0] = mShakePresses[1] = 0;
    resetState();
}

WPadSample* WPadReadDataInfo::getSample(u32 index) const {
    if (index >= mValidStatusCount) {
        return nullptr;
    }

    return &mStatusArray[index];
}

u32 WPadReadDataInfo::getValidStatusCount() const {
    return mValidStatusCount;
}

void WPadReadDataInfo::requestReset() {
    mResetRequested = true;
}

void WPadReadDataInfo::setButtonRepeat(f32 delaySec, f32 pulseSec) {
    if (pulseSec != 0.0f) {
        mRepeatDelay = static_cast< u16 >(static_cast< s32 >(delaySec * 200.0f + 0.5f));
        mRepeatPulse = static_cast< u16 >(static_cast< s32 >(pulseSec * 200.0f + 0.5f));
    } else {
        mRepeatDelay = cNoRepeatDelay;
        mRepeatPulse = 0;
    }

    mRepeatTime = 0;
    mRepeatNext = mRepeatDelay;
}

void WPadReadDataInfo::setAccelParam(f32 playRadius, f32 sensitivity) {
    mAccelPlayRadius = playRadius;
    mAccelSensitivity = sensitivity;
}

u32 WPadReadDataInfo::takeShakePresses(s32 device) {
    const u32 presses = mShakePresses[device];
    mShakePresses[device] = 0;
    return presses;
}

// The state a reset leaves (a scene change): nothing held, at rest with 1 g down, the pointer off at distance 1.
void WPadReadDataInfo::resetState() {
    mState.mHold = mState.mTrigger = mState.mRelease = 0;
    mState.mRepeat = false;
    mState.mIsPointerOn = false;
    mState.mPointerPos.set(0.0f, 0.0f);
    mState.mPointerSpeed = 0.0f;
    mState.mHorizon.set(1.0f, 0.0f);
    mState.mDistance = 1.0f;
    mState.mAccel.set(0.0f, 0.0f, 1.0f);
    mRepeatTime = 0;
    mRepeatNext = mRepeatDelay;
    mSubResetRequested = true;
}

// A connection or a Nunchuk change, told to WPad as the console's connect and extension callbacks told it.
void WPadReadDataInfo::updateConnection(s32 chan, const NebulaInputDevice& rDevice) {
    const bool wasConnected = mDevice.connected != 0;
    const bool hadSubPad = wasConnected && mDevice.nunchuk != 0;
    mDevice = rDevice;

    if (rDevice.connected == 0) {
        if (wasConnected) {
            WPad::connectCallback(chan, WPAD_ERR_NO_CONTROLLER);
        }

        return;
    }

    const bool subPad = rDevice.nunchuk != 0;

    if (!wasConnected) {
        NebulaFrameClock::resetIdle();
        WPad::connectCallback(chan, WPAD_ERR_NONE);
    } else if (subPad == hadSubPad) {
        return;
    }

    if (subPad) {
        WPad::extensionCallback(chan, WPAD_DEV_UNKNOWN);
    }

    WPad::extensionCallback(chan, subPad ? WPAD_DEV_FREESTYLE : WPAD_DEV_CORE);
}

void WPadReadDataInfo::watchActivity(const NebulaInputSample& rSample, bool subPad) {
    const NebulaInputSample& rPrevious = mActivitySample;
    bool active = rSample.buttons != rPrevious.buttons;

    if (exceeds(rSample.gravity, rPrevious.gravity, 3, cActivityAccel)) {
        active |= ++mAccelSettle > cActivitySettle;
    } else {
        mAccelSettle = 0;
    }

    if (subPad && exceeds(rSample.stick, rPrevious.stick, 2, cActivityStick)) {
        active |= ++mStickSettle > cActivitySettle;
    } else {
        mStickSettle = 0;
    }

    if (active) {
        mAccelSettle = 0;
        mStickSettle = 0;
        NebulaFrameClock::resetIdle();
    }

    mActivitySample = rSample;
}

// The samples since the last read as the game's own: the buttons of the newest with their edges and repeat; each
// sample's accelerations through the filter (acc += sensitivity * (reading - acc), full weight past the play radius
// and a fourth power of the distance inside it), its pointer (held through samples without one) and stick; shake
// presses counted. The read after a reset request returns none.
void WPadReadDataInfo::read(s32 chan) {
    const NebulaHostServices* host = nebula_host();
    NebulaInputDevice device = {};
    const u32 count = host->input_read(host->user, static_cast< u32 >(chan), &device, sSamples, WPAD_SAMPLE_MAX);
    updateConnection(chan, device);

    for (u32 i = 0; i < count; i++) {
        const u8 shake = sSamples[i].shake;

        for (s32 d = 0; d < 2; d++) {
            if ((shake & ~mShakeHeld) & (1u << d)) {
                mShakePresses[d]++;
            }
        }

        mShakeHeld = shake;
        watchActivity(sSamples[i], device.nunchuk != 0);
    }

    if (mResetRequested) {
        mResetRequested = false;
        resetState();
        mValidStatusCount = 0;
        return;
    }

    mValidStatusCount = count;

    if (count == 0) {
        return;
    }

    const bool subPad = device.nunchuk != 0;
    const u32 buttonMask = subPad ? 0xFFFFFFFFu : ~(NEBULA_BUTTON_C | NEBULA_BUTTON_Z);
    const u32 previous = mState.mHold;
    const u32 hold = sSamples[count - 1].buttons & buttonMask;
    const u32 change = hold ^ previous;
    mState.mHold = hold;
    mState.mTrigger = change & hold;
    mState.mRelease = change & previous;
    mState.mRepeat = false;

    if (mState.mTrigger != 0 || mState.mRelease != 0) {
        mRepeatTime = 0;
        mRepeatNext = mRepeatDelay;
        mState.mRepeat = mState.mTrigger != 0 && mRepeatPulse != 0;
    } else if (mState.mHold != 0) {
        mRepeatTime = static_cast< u16 >(mRepeatTime + count);

        if (mRepeatTime >= cNoRepeatDelay) {
            mRepeatTime = static_cast< u16 >(mRepeatTime - cNoRepeatDelay);
        }

        if (mRepeatTime >= mRepeatNext) {
            mState.mRepeat = true;
            mRepeatNext = static_cast< u16 >(mRepeatNext + mRepeatPulse);

            if (mRepeatTime >= cRepeatTimeMax) {
                mRepeatTime = static_cast< u16 >(mRepeatTime - cRepeatTimeMax);
                mRepeatNext = static_cast< u16 >(mRepeatNext - cRepeatTimeMax);
            }
        }
    }

    if (subPad != mStateSubPad) {
        mStateSubPad = subPad;
        mSubResetRequested = true;
    }

    mState.mIsSubPadConnected = subPad;

    for (u32 i = 0; i < count; i++) {
        const NebulaInputSample& rSample = sSamples[i];

        if (subPad) {
            if (mSubResetRequested) {
                mSubResetRequested = false;
                mState.mStick.set(0.0f, 0.0f);
                mState.mSubAccel.set(0.0f, 0.0f, 1.0f);
            }

            mState.mStick.set(rSample.stick[0], rSample.stick[1]);
        }

        for (s32 axis = 0; axis < 3; axis++) {
            f32* pAccel = &mState.mAccel.x + axis;
            f32* pSubAccel = &mState.mSubAccel.x + axis;
            const f32 accel = clampAccel(rSample.gravity[axis], cAccelMax);
            const f32 subAccel = clampAccel(rSample.nunchuk_gravity[axis], cSubAccelMax);
            const s32 filters = subPad ? 2 : 1;

            for (s32 f = 0; f < filters; f++) {
                f32* pValue = f == 0 ? pAccel : pSubAccel;
                const f32 diff = (f == 0 ? accel : subAccel) - *pValue;
                f32 weight = diff < 0.0f ? -diff : diff;

                if (weight >= mAccelPlayRadius) {
                    weight = 1.0f;
                } else {
                    weight /= mAccelPlayRadius;
                    weight *= weight;
                    weight *= weight;
                }

                weight *= mAccelSensitivity;
                *pValue += weight * diff;
            }
        }

        if (rSample.pointer_on != 0) {
            if (mState.mIsPointerOn) {
                const f32 dx = rSample.pointer[0] - mState.mPointerPos.x;
                const f32 dy = rSample.pointer[1] - mState.mPointerPos.y;
                mState.mPointerSpeed = sqrtf(dx * dx + dy * dy);
            } else {
                mState.mPointerSpeed = 0.0f;
            }

            mState.mPointerPos.set(rSample.pointer[0], rSample.pointer[1]);
            mState.mHorizon.set(rSample.roll[0], rSample.roll[1]);
            mState.mDistance = rSample.distance;
        }

        mState.mIsPointerOn = rSample.pointer_on != 0;
        mStatusArray[count - 1 - i] = mState;
    }
}

WPadHolder::WPadHolder() : mReadDataInfoArray(), mMode(WPAD_SENSOR_BAR_POS_TOP) {
    mReadDataInfoArray = new WPadReadDataInfo[WPAD_MAX_CONTROLLERS];

    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i] = new WPad(i, &mReadDataInfoArray[i]);
    }
}

void WPadHolder::updateReadDataOnly() {
    for (s32 chan = WPAD_CHAN0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        mReadDataInfoArray[chan].read(chan);
    }
}

void WPadHolder::updateProjectPadData() {
    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i]->update();
    }
}

// In play, a third or fourth controller is let go.
void WPadHolder::updateInGame() {
    for (s32 chan = WPAD_CHAN2; chan < WPAD_MAX_CONTROLLERS; chan++) {
        if (mReadDataInfoArray[chan].getSample(0) == nullptr) {
            continue;
        }

        const NebulaHostServices* host = nebula_host();
        host->input_control(host->user, static_cast< u32 >(chan), NEBULA_INPUT_RELEASE, 0);
    }
}

void WPadHolder::update() {
    updateReadDataOnly();
    updateProjectPadData();

    switch (mMode) {
    case WPAD_SENSOR_BAR_POS_TOP:
        updateInGame();
        break;
    case WPAD_SENSOR_BAR_POS_BOTTOM:
        break;
    }
}

// A scene change: each controller's motor stops, and its next read starts from rest.
void WPadHolder::resetPad() {
    for (s32 chan = WPAD_MAX_CONTROLLERS - 1; chan >= 0; chan--) {
        MR::setWPadMotor(chan, false);
        mReadDataInfoArray[chan].requestReset();
    }

    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i]->resetPad();
    }
}

WPad* WPadHolder::getWPad(s32 channel) {
    u32 channelMax = ARRAY_SIZE(mPad);

    if (channel < channelMax) {
        return mPad[channel];
    }

    return nullptr;
}

namespace {
    WPadHolder* getWPadHolder() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mWPadHolder;
    }
};  // namespace

namespace MR {
    WPad* getWPad(s32 channel) {
        return ::getWPadHolder()->getWPad(channel);
    }

    void resetWPad() {
        WPadHolder* pWPadHolder;

        pWPadHolder = ::getWPadHolder();
        pWPadHolder->resetPad();
    }

    void setWPadHolderModeHomeButton() {
        WPadHolder* pWPadHolder;

        pWPadHolder = ::getWPadHolder();
        pWPadHolder->mMode = WPAD_SENSOR_BAR_POS_BOTTOM;
    }

    void setWPadHolderModeGame() {
        WPadHolder* pWPadHolder;

        pWPadHolder = ::getWPadHolder();
        pWPadHolder->mMode = WPAD_SENSOR_BAR_POS_TOP;
    }


    void setAutoSleepTimeWiiRemote(bool isLongAutoSleep) {
        // Remotes sleep as the host decides.
        (void)isLongAutoSleep;
    }

    void setWPadMotor(s32 channel, bool on) {
        const NebulaHostServices* host = nebula_host();
        host->input_control(host->user, static_cast< u32 >(channel), NEBULA_INPUT_RUMBLE, on ? 1u : 0u);
    }
};  // namespace MR
