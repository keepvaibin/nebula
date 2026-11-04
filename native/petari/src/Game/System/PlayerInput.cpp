#include "Game/System/PlayerInput.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/System/InputTilt.hpp"
#include "Game/System/InputButtons.hpp"
#include "Game/System/InputSwing.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/InputInfoChecker.hpp"
#include "Game/System/InputPointer.hpp"
#include "Game/System/InputRumble.hpp"
#include "Game/System/InputStick.hpp"

PlayerInput::PlayerInput(s32 channel, PlayerInputReader* pReadInfo)
    : mChannel(channel), mReadInfo(pReadInfo), mButton(), mPointer(), mMainTilt(), mMainSwing(), _18(), _1C(), mStick(), mSubTilt(), mSubSwing(),
      mInfoChecker(), mIsConnected(), mHasMoveStick() {
    mButton = new InputButtons(this);
    mPointer = new InputPointer(this);
    mMainTilt = new InputTilt(this, INPUT_HAND_MAIN);
    mMainSwing = new InputSwing(this, INPUT_HAND_MAIN);
    _18 = new InputRumble(this);
    _1C = new InputRumble(this);
    _18->registInstance();
    mSubTilt = new InputTilt(this, INPUT_HAND_SUB);
    mSubSwing = new InputSwing(this, INPUT_HAND_SUB);
    mStick = new InputStick(this);
    mInfoChecker = new InputInfoChecker(this);
}

void PlayerInput::update() {
    if (mChannel == -1) {
        return;
    }

    mButton->update();
    mMainTilt->update();
    mPointer->update();
    mMainSwing->update();
    _18->update();
    _1C->update();
    mSubTilt->update();
    mSubSwing->update();
    mStick->update();
    mInfoChecker->update();
}

InputRumble* PlayerInput::getRumbleInstance() const {
    return _18->getRumbleInstance();
}

s32 PlayerInput::getBattery() const {
    return mInfoChecker->getBattery();
}

void PlayerInput::resetPad() {
}

void PlayerInput::connected() {
    mIsConnected = true;

    mInfoChecker->startCheck();
    SpkSystem::connect(mChannel);
}

void PlayerInput::disconnected() {
    mIsConnected = false;
    mHasMoveStick = false;

    mInfoChecker->reset();
    SpkSystem::disconnect(mChannel);
}

void PlayerInput::connectCallback(s32 chan, s32 reason) {
    PlayerInput* pInput = MR::getPlayerInput(chan);

    if (pInput == nullptr) {
        return;
    }

    switch (reason) {
    case INPUT_CONNECTED:
        pInput->connected();
        break;
    case INPUT_DISCONNECTED:
        pInput->disconnected();
        break;
    default:
        break;
    }
}

void PlayerInput::extensionCallback(s32 chan, s32 result) {
    PlayerInput* pInput = MR::getPlayerInput(chan);

    if (pInput == nullptr) {
        return;
    }

    SpkSystem::extensionProcess(chan, result);

    switch (result) {
    case INPUT_HAND_MAIN:
        pInput->mHasMoveStick = false;
        break;
    case INPUT_HAND_SUB:
        pInput->mHasMoveStick = true;
        break;
    case INPUT_HAND_UNKNOWN:
        break;
    }
}


bool MR::hasMoveStick(const PlayerInputSample* pSample) {
    return pSample != nullptr && pSample->mHasMoveStick;
}

PlayerInputSample* PlayerInput::getSample(u32 index) const {
    return mReadInfo->getSample(index);
}
