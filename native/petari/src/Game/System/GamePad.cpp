#include "Game/System/GamePad.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/System/GamePadAcceleration.hpp"
#include "Game/System/GamePadButton.hpp"
#include "Game/System/GamePadHVSwing.hpp"
#include "Game/System/GamePadHolder.hpp"
#include "Game/System/GamePadInfoChecker.hpp"
#include "Game/System/GamePadLeaveWatcher.hpp"
#include "Game/System/GamePadPointer.hpp"
#include "Game/System/GamePadRumble.hpp"
#include "Game/System/GamePadStick.hpp"

GamePad::GamePad(s32 channel, GamePadReadDataInfo* pReadInfo)
    : mChannel(channel), mReadInfo(pReadInfo), mButton(), mPointer(), mCorePadAccel(), mCorePadSwing(), _18(), _1C(), mStick(), mSubPadAccel(), mSubPadSwing(),
      mLeaveWatcher(), mInfoChecker(), _34(true), mIsConnected(), mIsSubPadConnected() {
    mButton = new GamePadButton(this);
    mPointer = new GamePadPointer(this);
    mCorePadAccel = new GamePadAcceleration(this, GAMEPAD_DEV_CORE);
    mCorePadSwing = new GamePadHVSwing(this, GAMEPAD_DEV_CORE);
    _18 = new GamePadRumble(this);
    _1C = new GamePadRumble(this);
    _18->registInstance();
    mSubPadAccel = new GamePadAcceleration(this, GAMEPAD_DEV_FREESTYLE);
    mSubPadSwing = new GamePadHVSwing(this, GAMEPAD_DEV_FREESTYLE);
    mStick = new GamePadStick(this);
    mLeaveWatcher = new GamePadLeaveWatcher(this);
    mInfoChecker = new GamePadInfoChecker(this);
    mSubPadSwing->mDistanceSwingThreshold = 2.0f;
}

void GamePad::update() {
    if (mChannel == -1) {
        return;
    }

    _34 = false;

    mButton->update();
    mCorePadAccel->update();
    mPointer->update();
    mCorePadSwing->update();
    _18->update();
    _1C->update();
    mSubPadAccel->update();
    mSubPadSwing->update();
    mStick->update();
    mLeaveWatcher->update();
    mInfoChecker->update();

    _34 = true;
}

GamePadRumble* GamePad::getRumbleInstance() const {
    return _18->getRumbleInstance();
}

void GamePad::startLeaveWatcher() {
    mLeaveWatcher->start();
}

void GamePad::stopLeaveWatcher() {
    mLeaveWatcher->stop();
}

void GamePad::restartLeaveWatcher() {
    mLeaveWatcher->restart();
}

s32 GamePad::getBattery() const {
    return mInfoChecker->getBattery();
}

void GamePad::resetPad() {
}

void GamePad::connected() {
    mIsConnected = true;

    mInfoChecker->startCheck();
    SpkSystem::connect(mChannel);
}

void GamePad::disconnected() {
    mIsConnected = false;
    mIsSubPadConnected = false;

    mInfoChecker->reset();
    SpkSystem::disconnect(mChannel);
}

void GamePad::connectCallback(s32 chan, s32 reason) {
    GamePad* pGamePad = MR::getGamePad(chan);

    if (pGamePad == nullptr) {
        return;
    }

    switch (reason) {
    case GAMEPAD_ERR_NONE:
        pGamePad->connected();
        break;
    case GAMEPAD_ERR_NO_CONTROLLER:
        pGamePad->disconnected();
        break;
    default:
        break;
    }
}

void GamePad::extensionCallback(s32 chan, s32 result) {
    GamePad* pGamePad = MR::getGamePad(chan);

    if (pGamePad == nullptr) {
        return;
    }

    SpkSystem::extensionProcess(chan, result);

    switch (result) {
    case GAMEPAD_DEV_CORE:
        pGamePad->mIsSubPadConnected = false;
        break;
    case GAMEPAD_DEV_FREESTYLE:
        pGamePad->mIsSubPadConnected = true;
        break;
    case GAMEPAD_DEV_UNKNOWN:
        break;
    }
}


bool MR::isDeviceFreeStyle(const GamePadSample* pSample) {
    return pSample != nullptr && pSample->mIsSubPadConnected;
}

GamePadSample* GamePad::getSample(u32 index) const {
    return mReadInfo->getSample(index);
}

s32 GamePad::getValidStatusCount() const {
    return mReadInfo->getValidStatusCount();
}

s32 GamePad::getEnableAccelPastCount(u32 deviceType) const {
    GamePadAcceleration* pAcceleration;

    if (deviceType == GAMEPAD_DEV_CORE) {
        pAcceleration = mCorePadAccel;
    } else {
        pAcceleration = mSubPadAccel;
    }

    return pAcceleration->_628;
}

bool GamePad::getAcceleration(TVec3f* pAccel, u32 deviceType) const {
    GamePadAcceleration* pAcceleration;

    if (deviceType == GAMEPAD_DEV_CORE) {
        pAcceleration = mCorePadAccel;
    } else {
        pAcceleration = mSubPadAccel;
    }

    return pAcceleration->getAcceleration(pAccel);
}

bool GamePad::getPastAcceleration(TVec3f* pAccel, s32 param2, u32 deviceType) const {
    GamePadAcceleration* pAcceleration;

    if (deviceType == GAMEPAD_DEV_CORE) {
        pAcceleration = mCorePadAccel;
    } else {
        pAcceleration = mSubPadAccel;
    }

    return pAcceleration->getPastAcceleration(pAccel, param2);
}
