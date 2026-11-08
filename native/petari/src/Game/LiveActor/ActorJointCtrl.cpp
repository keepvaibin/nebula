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

void ActorJointCtrl::startDynamicCtrl(const char* pName, s32 val) {
    mJointCtrlKeeper->startCtrl(pName, val);
}
void ActorJointCtrl::endDynamicCtrl(const char* pName, s32 val) {
    mJointCtrlKeeper->endCtrl(pName, val);
}

void ActorJointCtrl::resetDynamicCtrl() {
    mJointCtrlKeeper->reset();
}

void ActorJointCtrl::startFaceCtrl(s32 val) {
    mFaceJointCtrl->startCtrl(val);
}

void ActorJointCtrl::endFaceCtrl(s32 val) {
    mFaceJointCtrl->endCtrl(val);
}

void ActorJointCtrl::setIKEndPosition(const char* pName, const Vec3f& a2, f32 a3) {
    mJointCtrlHolder->setEndPosition(pName, a2, a3);
}

void ActorJointCtrl::setIKEndDirection(const char* pName, const Vec3f& a2, f32 a3) {
    mJointCtrlHolder->setEndDirection(pName, a2, a3);
}

void ActorJointCtrl::endIKCtrlAll() {
    mJointCtrlHolder->endCtrlAll();
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