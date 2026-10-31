#include "Game/System/WPad.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/System/WPadAcceleration.hpp"
#include "Game/System/WPadButton.hpp"
#include "Game/System/WPadHVSwing.hpp"
#include "Game/System/WPadHolder.hpp"
#include "Game/System/WPadInfoChecker.hpp"
#include "Game/System/WPadLeaveWatcher.hpp"
#include "Game/System/WPadPointer.hpp"
#include "Game/System/WPadRumble.hpp"
#include "Game/System/WPadStick.hpp"

WPad::WPad(s32 channel, WPadReadDataInfo* pReadInfo)
    : mChannel(channel), mReadInfo(pReadInfo), mButton(), mPointer(), mCorePadAccel(), mCorePadSwing(), _18(), _1C(), mStick(), mSubPadAccel(), mSubPadSwing(),
      mLeaveWatcher(), mInfoChecker(), _34(true), mIsConnected(), mIsSubPadConnected() {
    mButton = new WPadButton(this);
    mPointer = new WPadPointer(this);
    mCorePadAccel = new WPadAcceleration(this, WPAD_DEV_CORE);
    mCorePadSwing = new WPadHVSwing(this, WPAD_DEV_CORE);
    _18 = new WPadRumble(this);
    _1C = new WPadRumble(this);
    _18->registInstance();
    mSubPadAccel = new WPadAcceleration(this, WPAD_DEV_FREESTYLE);
    mSubPadSwing = new WPadHVSwing(this, WPAD_DEV_FREESTYLE);
    mStick = new WPadStick(this);
    mLeaveWatcher = new WPadLeaveWatcher(this);
    mInfoChecker = new WPadInfoChecker(this);
    mSubPadSwing->mDistanceSwingThreshold = 2.0f;
}

void WPad::update() {
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

WPadRumble* WPad::getRumbleInstance() const {
    return _18->getRumbleInstance();
}

void WPad::startLeaveWatcher() {
    mLeaveWatcher->start();
}

void WPad::stopLeaveWatcher() {
    mLeaveWatcher->stop();
}

void WPad::restartLeaveWatcher() {
    mLeaveWatcher->restart();
}

s32 WPad::getBattery() const {
    return mInfoChecker->getBattery();
}

void WPad::resetPad() {
}

void WPad::connected() {
    mIsConnected = true;

    mInfoChecker->startCheck();
    SpkSystem::connect(mChannel);
}

void WPad::disconnected() {
    mIsConnected = false;
    mIsSubPadConnected = false;

    mInfoChecker->reset();
    SpkSystem::disconnect(mChannel);
}

void WPad::connectCallback(s32 chan, s32 reason) {
    WPad* pWPad = MR::getWPad(chan);

    if (pWPad == nullptr) {
        return;
    }

    switch (reason) {
    case WPAD_ERR_NONE:
        pWPad->connected();
        break;
    case WPAD_ERR_NO_CONTROLLER:
        pWPad->disconnected();
        break;
    default:
        break;
    }
}

void WPad::extensionCallback(s32 chan, s32 result) {
    WPad* pWPad = MR::getWPad(chan);

    if (pWPad == nullptr) {
        return;
    }

    SpkSystem::extensionProcess(chan, result);

    switch (result) {
    case WPAD_DEV_CORE:
        pWPad->mIsSubPadConnected = false;
        break;
    case WPAD_DEV_FREESTYLE:
        pWPad->mIsSubPadConnected = true;
        break;
    case WPAD_DEV_UNKNOWN:
        break;
    }
}


bool MR::isDeviceFreeStyle(const WPadSample* pSample) {
    return pSample != nullptr && pSample->mIsSubPadConnected;
}

WPadSample* WPad::getSample(u32 index) const {
    return mReadInfo->getSample(index);
}

s32 WPad::getValidStatusCount() const {
    return mReadInfo->getValidStatusCount();
}

s32 WPad::getEnableAccelPastCount(u32 deviceType) const {
    WPadAcceleration* pAcceleration;

    if (deviceType == WPAD_DEV_CORE) {
        pAcceleration = mCorePadAccel;
    } else {
        pAcceleration = mSubPadAccel;
    }

    return pAcceleration->_628;
}

bool WPad::getAcceleration(TVec3f* pAccel, u32 deviceType) const {
    WPadAcceleration* pAcceleration;

    if (deviceType == WPAD_DEV_CORE) {
        pAcceleration = mCorePadAccel;
    } else {
        pAcceleration = mSubPadAccel;
    }

    return pAcceleration->getAcceleration(pAccel);
}

bool WPad::getPastAcceleration(TVec3f* pAccel, s32 param2, u32 deviceType) const {
    WPadAcceleration* pAcceleration;

    if (deviceType == WPAD_DEV_CORE) {
        pAcceleration = mCorePadAccel;
    } else {
        pAcceleration = mSubPadAccel;
    }

    return pAcceleration->getPastAcceleration(pAccel, param2);
}
