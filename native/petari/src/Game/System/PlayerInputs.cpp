#include "Game/System/PlayerInputs.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/InputPointer.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <math.h>
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
    // PlayerInputs::mPad's players.
    const u32 cGamePlayers = 2;
    static_assert(sizeof(PlayerInputs::mPad) / sizeof(PlayerInput*) == cGamePlayers, "the game's pads");
    // NebulaInputPlayers::count once asked (0: not yet).
    u32 sPlayers;
}  // namespace

u32 NebulaInputPlayers::count() {
    if (sPlayers == 0) {
        const u32 host = NebulaHost::inputPlayers();
        sPlayers = host > cGamePlayers ? host : cGamePlayers;
    }
    return sPlayers;
}

PlayerInputReader::PlayerInputReader() : mDevice(), mState(), mValid(false), mResetRequested(true) {
    mSpinPresses[0] = mSpinPresses[1] = 0;
    resetState();
}

PlayerInputSample* PlayerInputReader::getSample() {
    return mValid ? &mState : nullptr;
}

void PlayerInputReader::requestReset() {
    mResetRequested = true;
}

u32 PlayerInputReader::takeSpinPresses(s32 hand) {
    const u32 presses = mSpinPresses[hand];
    mSpinPresses[hand] = 0;
    return presses;
}

// The state a reset leaves (a scene change): nothing held, the controller at rest, the pointer off.
void PlayerInputReader::resetState() {
    mState.mHold = mState.mTrigger = mState.mRelease = 0;
    mState.mIsPointerOn = false;
    mState.mPointerPos.set(0.0f, 0.0f);
    mState.mHorizon.set(1.0f, 0.0f);
    mState.mDistance = 1.0f;
    mState.mAccel.set(0.0f, 0.0f, 1.0f);
    mState.mSubTilt.set(0.0f, 0.0f, 1.0f);
    mState.mStick.set(0.0f, 0.0f);
}

// A connection or a move stick change, told to PlayerInput.
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

void PlayerInputReader::read(s32 chan) {
    const NebulaHostServices* host = nebula_host();
    NebulaInputDevice device = {};
    NebulaInputSample sample;
    const bool has = host->input_read(host->user, static_cast< u32 >(chan), &device, &sample) != 0;
    updateConnection(chan, device);
    if (has) {
        mSpinPresses[0] += sample.spin_presses;
        mSpinPresses[1] += sample.sub_spin_presses;
    }

    if (mResetRequested) {
        mResetRequested = false;
        host->input_control(host->user, static_cast< u32 >(chan), NEBULA_INPUT_RESET, 0);
        resetState();
        mValid = false;
        return;
    }

    mValid = has;
    if (!has) {
        return;
    }

    const bool subPad = device.nunchuk != 0;
    const u32 buttonMask = subPad ? 0xFFFFFFFFu : ~(NEBULA_BUTTON_C | NEBULA_BUTTON_Z);
    const u32 previous = mState.mHold;
    const u32 hold = sample.buttons & buttonMask;
    const u32 change = hold ^ previous;
    mState.mHold = hold;
    mState.mTrigger = change & hold;
    mState.mRelease = change & previous;
    mState.mHasMoveStick = subPad;
    mState.mStick.set(subPad ? sample.stick[0] : 0.0f, subPad ? sample.stick[1] : 0.0f);
    mState.mAccel.set(sample.tilt[0], sample.tilt[1], sample.tilt[2]);
    if (subPad) {
        mState.mSubTilt.set(sample.sub_tilt[0], sample.sub_tilt[1], sample.sub_tilt[2]);
    }
    mState.mIsPointerOn = sample.pointer_on != 0;
    if (mState.mIsPointerOn) {
        mState.mPointerPos.set(sample.pointer[0], sample.pointer[1]);
        mState.mHorizon.set(sample.roll[0], sample.roll[1]);
        mState.mDistance = sample.distance;
    }
}

PlayerInputs::PlayerInputs() : mReadDataInfoArray(), mMode(INPUT_MODE_GAME) {
    mReadDataInfoArray = new PlayerInputReader[NebulaInputPlayers::count()];

    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i] = new PlayerInput(i, &mReadDataInfoArray[i]);
    }
}

void PlayerInputs::updateReadDataOnly() {
    for (s32 chan = PLAYER_1; chan < static_cast< s32 >(NebulaInputPlayers::count()); chan++) {
        mReadDataInfoArray[chan].read(chan);
    }
}

void PlayerInputs::updateProjectPadData() {
    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i]->update();
    }
}

// In play, a controller past the game's two is let go.
void PlayerInputs::updateInGame() {
    const NebulaHostServices* const host = nebula_host();
    for (s32 chan = PLAYER_3; chan < static_cast< s32 >(NebulaInputPlayers::count()); chan++) {
        if (mReadDataInfoArray[chan].getSample() != nullptr) {
            host->input_control(host->user, static_cast< u32 >(chan), NEBULA_INPUT_RELEASE, 0);
        }
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
    for (s32 chan = static_cast< s32 >(NebulaInputPlayers::count()) - 1; chan >= 0; chan--) {
        MR::setInputRumble(chan, 0);
        mReadDataInfoArray[chan].requestReset();
    }

    for (u32 i = 0; i < ARRAY_SIZE(mPad); i++) {
        mPad[i]->resetPad();
    }
}

PlayerInput* PlayerInputs::getPlayerInput(s32 channel) {
    // A negative channel is out of range too.
    return static_cast< u32 >(channel) < ARRAY_SIZE(mPad) ? mPad[channel] : nullptr;
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
        ::getPlayerInputs()->resetPad();
    }

    void setPlayerInputsModeHome() {
        ::getPlayerInputs()->mMode = INPUT_MODE_HOME;
    }

    void setPlayerInputsModeGame() {
        ::getPlayerInputs()->mMode = INPUT_MODE_GAME;
    }

    void setInputRumble(s32 channel, u8 strength) {
        const NebulaHostServices* host = nebula_host();
        host->input_control(host->user, static_cast< u32 >(channel), NEBULA_INPUT_RUMBLE, strength);
    }
};  // namespace MR
