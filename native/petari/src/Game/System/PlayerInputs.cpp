#include "Game/System/PlayerInputs.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/InputPointer.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <math.h>
#include "Game/System/NebulaInput.hpp"

#define INPUT_SAMPLE_MAX 120

namespace {
    // The largest acceleration the main hand's and the sub hand's sensors read, in g.
    const f32 cAccelMax = 3.4f;
    const f32 cTiltFollow = 0.15f;  // of the difference, each sample
    const f32 cSubAccelMax = 2.1f;

    NebulaInputSample sSamples[INPUT_SAMPLE_MAX];

    f32 clampAccel(f32 value, f32 limit) {
        return value < -limit ? -limit : value > limit ? limit : value;
    }
};  // namespace

PlayerInputReader::PlayerInputReader()
    : mStatusArray(), mValidStatusCount(), mDevice(), mState(), mResetRequested(true), mStateMoveStick(false),
      mSubHandResetRequested(true), mShakeHeld(0) {
    mStatusArray = new PlayerInputSample[INPUT_SAMPLE_MAX];
    MR::zeroMemory(mStatusArray, sizeof(PlayerInputSample) * INPUT_SAMPLE_MAX);
    mShakePresses[0] = mShakePresses[1] = 0;
    resetState();
}

PlayerInputSample* PlayerInputReader::getSample(u32 index) const {
    if (index >= mValidStatusCount) {
        return nullptr;
    }

    return &mStatusArray[index];
}

u32 PlayerInputReader::getValidStatusCount() const {
    return mValidStatusCount;
}

void PlayerInputReader::requestReset() {
    mResetRequested = true;
}

u32 PlayerInputReader::takeShakePresses(s32 device) {
    const u32 presses = mShakePresses[device];
    mShakePresses[device] = 0;
    return presses;
}

// The state a reset leaves (a scene change): nothing held, at rest with 1 g down, the pointer off at distance 1.
void PlayerInputReader::resetState() {
    mState.mHold = mState.mTrigger = mState.mRelease = 0;
    mState.mIsPointerOn = false;
    mState.mPointerPos.set(0.0f, 0.0f);
    mState.mHorizon.set(1.0f, 0.0f);
    mState.mDistance = 1.0f;
    mState.mAccel.set(0.0f, 0.0f, 1.0f);
    mSubHandResetRequested = true;
}

// A connection or a move stick change, told to PlayerInput as the console's connect and extension callbacks told it.
void PlayerInputReader::updateConnection(s32 chan, const NebulaInputDevice& rDevice) {
    const bool wasConnected = mDevice.connected != 0;
    const bool hadSubPad = wasConnected && mDevice.nunchuk != 0;
    mDevice = rDevice;

    if (rDevice.connected == 0) {
        if (wasConnected) {
            PlayerInput::connectCallback(chan, INPUT_DISCONNECTED);
        }

        return;
    }

    const bool subPad = rDevice.nunchuk != 0;

    if (!wasConnected) {
        PlayerInput::connectCallback(chan, INPUT_CONNECTED);
    } else if (subPad == hadSubPad) {
        return;
    }

    if (subPad) {
        PlayerInput::extensionCallback(chan, INPUT_HAND_UNKNOWN);
    }

    PlayerInput::extensionCallback(chan, subPad ? INPUT_HAND_SUB : INPUT_HAND_MAIN);
}

// The samples since the last read as the game's own: the buttons of the newest with their edges and repeat; each
// sample's accelerations through the filter (acc += sensitivity * (reading - acc), full weight past the play radius
// and a fourth power of the distance inside it), its pointer (held through samples without one) and stick; shake
// presses counted. The read after a reset request returns none.
void PlayerInputReader::read(s32 chan) {
    const NebulaHostServices* host = nebula_host();
    NebulaInputDevice device = {};
    const u32 count = host->input_read(host->user, static_cast< u32 >(chan), &device, sSamples, INPUT_SAMPLE_MAX);
    updateConnection(chan, device);

    for (u32 i = 0; i < count; i++) {
        const u8 shake = sSamples[i].shake;

        for (s32 d = 0; d < 2; d++) {
            if ((shake & ~mShakeHeld) & (1u << d)) {
                mShakePresses[d]++;
            }
        }

        mShakeHeld = shake;
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

    if (subPad != mStateMoveStick) {
        mStateMoveStick = subPad;
        mSubHandResetRequested = true;
    }

    mState.mHasMoveStick = subPad;

    for (u32 i = 0; i < count; i++) {
        const NebulaInputSample& rSample = sSamples[i];

        if (subPad) {
            if (mSubHandResetRequested) {
                mSubHandResetRequested = false;
                mState.mStick.set(0.0f, 0.0f);
                mState.mSubTilt.set(0.0f, 0.0f, 1.0f);
            }

            mState.mStick.set(rSample.stick[0], rSample.stick[1]);
        }

        for (s32 axis = 0; axis < 3; axis++) {
            f32* pAccel = &mState.mAccel.x + axis;
            f32* pSubAccel = &mState.mSubTilt.x + axis;
            const f32 accel = clampAccel(rSample.gravity[axis], cAccelMax);
            const f32 subAccel = clampAccel(rSample.nunchuk_gravity[axis], cSubAccelMax);
            const s32 filters = subPad ? 2 : 1;

            for (s32 f = 0; f < filters; f++) {
                f32* pValue = f == 0 ? pAccel : pSubAccel;
                *pValue += cTiltFollow * ((f == 0 ? accel : subAccel) - *pValue);
            }
        }

        if (rSample.pointer_on != 0) {
            mState.mPointerPos.set(rSample.pointer[0], rSample.pointer[1]);
            mState.mHorizon.set(rSample.roll[0], rSample.roll[1]);
            mState.mDistance = rSample.distance;
        }

        mState.mIsPointerOn = rSample.pointer_on != 0;
        mStatusArray[count - 1 - i] = mState;
    }
}

PlayerInputs::PlayerInputs() : mReadDataInfoArray(), mMode(INPUT_MODE_GAME) {
    mReadDataInfoArray = new PlayerInputReader[PLAYER_MAX];

    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i] = new PlayerInput(i, &mReadDataInfoArray[i]);
    }
}

void PlayerInputs::updateReadDataOnly() {
    for (s32 chan = PLAYER_1; chan < PLAYER_MAX; chan++) {
        mReadDataInfoArray[chan].read(chan);
    }
}

void PlayerInputs::updateProjectPadData() {
    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i]->update();
    }
}

// In play, a third or fourth controller is let go.
void PlayerInputs::updateInGame() {
    for (s32 chan = PLAYER_3; chan < PLAYER_MAX; chan++) {
        if (mReadDataInfoArray[chan].getSample(0) == nullptr) {
            continue;
        }

        const NebulaHostServices* host = nebula_host();
        host->input_control(host->user, static_cast< u32 >(chan), NEBULA_INPUT_RELEASE, 0);
    }
}

void PlayerInputs::update() {
    updateReadDataOnly();
    updateProjectPadData();

    switch (mMode) {
    case INPUT_MODE_GAME:
        updateInGame();
        break;
    case INPUT_MODE_HOME:
        break;
    }
}

// A scene change: each controller's motor stops, and its next read starts from rest.
void PlayerInputs::resetPad() {
    for (s32 chan = PLAYER_MAX - 1; chan >= 0; chan--) {
        MR::setInputRumble(chan, false);
        mReadDataInfoArray[chan].requestReset();
    }

    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i]->resetPad();
    }
}

PlayerInput* PlayerInputs::getPlayerInput(s32 channel) {
    u32 channelMax = ARRAY_SIZE(mPad);

    if (channel < channelMax) {
        return mPad[channel];
    }

    return nullptr;
}

namespace {
    PlayerInputs* getPlayerInputs() {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mPlayerInputs;
    }
};  // namespace

namespace MR {
    PlayerInput* getPlayerInput(s32 channel) {
        return ::getPlayerInputs()->getPlayerInput(channel);
    }

    void resetPlayerInput() {
        PlayerInputs* pInputs;

        pInputs = ::getPlayerInputs();
        pInputs->resetPad();
    }

    void setPlayerInputsModeHome() {
        PlayerInputs* pInputs;

        pInputs = ::getPlayerInputs();
        pInputs->mMode = INPUT_MODE_HOME;
    }

    void setPlayerInputsModeGame() {
        PlayerInputs* pInputs;

        pInputs = ::getPlayerInputs();
        pInputs->mMode = INPUT_MODE_GAME;
    }


    void setInputSleepTime(bool isLongAutoSleep) {
        // Controllers sleep as the host decides.
        (void)isLongAutoSleep;
    }

    void setInputRumble(s32 channel, bool on) {
        const NebulaHostServices* host = nebula_host();
        host->input_control(host->user, static_cast< u32 >(channel), NEBULA_INPUT_RUMBLE, on ? 1u : 0u);
    }
};  // namespace MR
