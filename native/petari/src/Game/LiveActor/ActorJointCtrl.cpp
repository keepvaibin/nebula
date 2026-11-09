#include "Game/LiveActor/ActorJointCtrl.hpp"
#include "Game/LiveActor/DynamicJointCtrl.hpp"
#include "Game/LiveActor/FaceJointCtrl.hpp"
#include "Game/LiveActor/IKJointCtrl.hpp"
#include "Game/LiveActor/LiveActor.hpp"

ActorJointCtrl::ActorJointCtrl(LiveActor* pActor) : mActor(pActor), mIsIdle(true) {
    mJointCtrlKeeper = new DynamicJointCtrlKeeper(mActor);
    mFaceJointCtrl = new FaceJointCtrl(mActor);
    mFaceJointCtrl->init();
    mJointCtrlHolder = ActorJoint::createIKJointCtrlHolder(mActor);
}

void ActorJointCtrl::startDynamicCtrl(const char* pName, s32 time) {
    mJointCtrlKeeper->startCtrl(pName, time);
}

void ActorJointCtrl::endDynamicCtrl(const char* pName, s32 time) {
    mJointCtrlKeeper->endCtrl(pName, time);
}

void ActorJointCtrl::resetDynamicCtrl() {
    mJointCtrlKeeper->reset();
}

void ActorJointCtrl::startFaceCtrl(s32 time) {
    mFaceJointCtrl->startCtrl(time);
}

void ActorJointCtrl::endFaceCtrl(s32 time) {
    mFaceJointCtrl->endCtrl(time);
}

void ActorJointCtrl::setIKEndPosition(const char* pName, const Vec3f& rPos, f32 rate) {
    // An actor with no IK table has nothing to bend.
    if (mJointCtrlHolder != nullptr) {
        mJointCtrlHolder->setEndPosition(pName, rPos, rate);
    }
}

void ActorJointCtrl::setIKEndDirection(const char* pName, const Vec3f& rDirection, f32 rate) {
    if (mJointCtrlHolder != nullptr) {
        mJointCtrlHolder->setEndDirection(pName, rDirection, rate);
    }
}

void ActorJointCtrl::endIKCtrlAll() {
    if (mJointCtrlHolder != nullptr) {
        mJointCtrlHolder->endCtrlAll();
    }
}

void ActorJointCtrl::update() {
    startUpdate();
    endUpdate();
}

void ActorJointCtrl::startUpdate() {
    if (mJointCtrlHolder) {
        mJointCtrlHolder->startUpdate();
        mActor->calcAnim();
    }

    mIsIdle = false;
}

void ActorJointCtrl::endUpdate() {
    if (mJointCtrlKeeper) {
        mJointCtrlKeeper->update();
    }

    if (mFaceJointCtrl) {
        mFaceJointCtrl->update();
    }

    if (mJointCtrlHolder) {
        mJointCtrlHolder->endUpdate();
    }

    mIsIdle = true;
}

void ActorJointCtrl::setCallBackFunction() {
    if (mIsIdle) {
        if (mJointCtrlKeeper) {
            mJointCtrlKeeper->setCallBackFunction();
        }

        if (mFaceJointCtrl) {
            mFaceJointCtrl->setCallBackFunction();
        }

        if (mJointCtrlHolder) {
            mJointCtrlHolder->setCallBackFunction();
        }
    }
}