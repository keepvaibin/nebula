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
    : mChannel(channel), mReadInfo(pReadInfo), mButton(), mPointer(), mMainTilt(), mMainSwing(), mRumble(), mMenuRumble(), mStick(), mSubTilt(), mSubSwing(),
      mInfoChecker(), mIsConnected(), mHasMoveStick() {
    mButton = new InputButtons(this);
    mPointer = new InputPointer(this);
    mMainTilt = new InputTilt(this, INPUT_HAND_MAIN);
    mMainSwing = new InputSwing(this, INPUT_HAND_MAIN);
    mRumble = new InputRumble(this);
    mMenuRumble = new InputRumble(this);
    mRumble->registInstance();
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
    mRumble->update();
    mMenuRumble->update();
    mSubTilt->update();
    mSubSwing->update();
    mStick->update();
    mInfoChecker->update();
}

InputRumble* PlayerInput::getRumbleInstance() const {
    return mRumble->getRumbleInstance();
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

PlayerInputSample* PlayerInput::getSample() const {
    return mReadInfo->getSample();
}
