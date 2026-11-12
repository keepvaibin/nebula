#include "Game/LiveActor/IKJointCtrl.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util.hpp"
#include "Game/Render/ModelData.hpp"

namespace {
    const char* sTextOutFileName = "IKJointCtrl";
};  // namespace

IKJointCtrl::IKJointCtrl(LiveActor* pActor)
    : mName(), mActor(pActor), mJoint(), mRootCtrl(), mMiddleCtrl(), mEndCtrl(), mEndDir(0.0f, 1.0f, 0.0f), mEndLocalDir(0.0f, 1.0f, 0.0f),
      mEndDirMaxDegree(45.0f), mEndPosRate(), mEndDirRate(), mRootJointNo(NEBULA_NONE32), mMiddleJointNo(NEBULA_NONE32), mEndJointNo(NEBULA_NONE32),
      mIsCallBackEnabled() {
}

void IKJointCtrl::init(const IKJointCtrlParam& rParam) {
    mName = rParam.mName;
    mEndLocalDir = rParam.mEndLocalDir;
    MR::normalize(&mEndLocalDir);
    mEndDirMaxDegree = rParam.mEndDirMaxDegree;

    Joint* endJoint = MR::getJoint(mActor, mName);
    Joint* middleJoint = MR::getParentJoint(mActor, endJoint);
    Joint* rootJoint = MR::getParentJoint(mActor, middleJoint);
    mJoint = new IKJoint;
    mRootJointNo = rootJoint->getJntNo();
    mMiddleJointNo = middleJoint->getJntNo();
    mEndJointNo = endJoint->getJntNo();
    mRootCtrl = MR::createJointController< IKJointCtrl >(this, mActor, mRootJointNo, &IKJointCtrl::updateRootJointCallBack, nullptr);
    mMiddleCtrl = MR::createJointController< IKJointCtrl >(this, mActor, mMiddleJointNo, &IKJointCtrl::updateMiddleJointCallBack, nullptr);
    mEndCtrl = MR::createJointController< IKJointCtrl >(this, mActor, mEndJointNo, &IKJointCtrl::updateEndJointCallBack, nullptr);
}

void IKJointCtrl::setEndPosition(const Vec3f& rPos, f32 rate) {
    Vec3f jointPos;
    MR::copyJointPos(mActor, mEndJointNo, &jointPos);
    MR::vecBlend(jointPos, rPos, &jointPos, rate);
    mJoint->update(MR::getJointMtx(mActor, mRootJointNo), MR::getJointMtx(mActor, mMiddleJointNo), MR::getJointMtx(mActor, mEndJointNo), jointPos);
    mEndPosRate = rate;
}

void IKJointCtrl::setEndDirection(const Vec3f& rDirection, f32 rate) {
    if (!MR::vecBlendSphere(mEndDir, rDirection, &mEndDir, rate)) {
        MR::turnRandomVector(&mEndDir, mEndDir, 0.01f);
    }

    mEndDirRate = rate;
}

void IKJointCtrl::endCtrl() {
    mEndPosRate = 0.0f;
    mEndDirRate = 0.0f;
}

void IKJointCtrl::setCallBackFunction() {
    if (!mIsCallBackEnabled) {
        return;
    }

    if (mEndPosRate > 0.0f) {
        mRootCtrl->registerCallBack();
        mMiddleCtrl->registerCallBack();
    }

    if (mEndDirRate > 0.0f) {
        mEndCtrl->registerCallBack();
    }
}

bool IKJointCtrl::updateRootJointCallBack(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    *pPos = mJoint->mRootMtx;
    return true;
}

bool IKJointCtrl::updateMiddleJointCallBack(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    *pPos = mJoint->mMiddleMtx;
    return true;
}

bool IKJointCtrl::updateEndJointCallBack(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    MR::turnMtxToLocalDirDegree(pMtx, mEndLocalDir, mEndDir, mEndDirMaxDegree * mEndDirRate);
    return true;
}

IKJointCtrlHolder::IKJointCtrlHolder(LiveActor* pActor, s32 capacity)
    : mControls(new IKJointCtrl*[capacity]()), mCapacity(capacity), mNumControls(), mActor(pActor), mIsIdle(true) {
}

void IKJointCtrlHolder::addCtrl(const IKJointCtrlParam& rParam) {
    if (mNumControls >= mCapacity) {
        mControls = MR::growArray(mControls, mCapacity);
        mCapacity *= 2;
    }

    // A joint without two parents (a bad name in the table) has no chain to bend: no control is made for it.
    Joint* const pEndJoint = MR::getJoint(mActor, rParam.mName);
    Joint* const pMiddleJoint = MR::getParentJoint(mActor, pEndJoint);
    if (pMiddleJoint == nullptr || MR::getParentJoint(mActor, pMiddleJoint) == nullptr) {
        return;
    }

    IKJointCtrl* ctrl = new IKJointCtrl(mActor);
    ctrl->init(rParam);
    mControls[mNumControls++] = ctrl;
}

void IKJointCtrlHolder::startUpdate() {
    mIsIdle = false;
    for (s32 i = 0; i < mNumControls; i++) {
        mControls[i]->disableCallBack();
    }
}

void IKJointCtrlHolder::setEndPosition(const char* pName, const Vec3f& rPos, f32 rate) {
    if (IKJointCtrl* const pCtrl = findIKJointCtrl(pName)) {
        pCtrl->setEndPosition(rPos, rate);
    }
}

void IKJointCtrlHolder::setEndDirection(const char* pName, const Vec3f& rDirection, f32 rate) {
    if (IKJointCtrl* const pCtrl = findIKJointCtrl(pName)) {
        pCtrl->setEndDirection(rDirection, rate);
    }
}

void IKJointCtrlHolder::endUpdate() {
    for (s32 i = 0; i < mNumControls; i++) {
        mControls[i]->enableCallBack();
    }
    mIsIdle = true;
}

void IKJointCtrlHolder::endCtrlAll() {
    for (s32 i = 0; i < mNumControls; i++) {
        mControls[i]->endCtrl();
    }
}

void IKJointCtrlHolder::setCallBackFunction() {
    if (!mIsIdle) {
        return;
    }

    for (s32 i = 0; i < mNumControls; i++) {
        mControls[i]->setCallBackFunction();
    }
}

IKJointCtrl* IKJointCtrlHolder::findIKJointCtrl(const char* pName) {
    for (s32 i = 0; i < mNumControls; i++) {
        if (MR::isEqualString(pName, mControls[i]->mName)) {
            return mControls[i];
        }
    }

    return nullptr;
}

IKJointCtrlHolder* ActorJoint::createIKJointCtrlHolder(LiveActor* pActor) {
    const ResourceHolder* resourceHolder = MR::getResourceHolder(pActor);
    Table* csv = MR::tryCreateCsvParser(resourceHolder, "%s.bcsv", ::sTextOutFileName);
    if (!csv) {
        return nullptr;
    }

    s32 count = csv->getNumEntries();

    if (count <= 0) {
        return nullptr;
    }

    IKJointCtrlHolder* holder = new IKJointCtrlHolder(pActor, count);

    for (s32 i = 0; i < count; i++) {
        IKJointCtrlParam param;
        csv->getValue(i, "JointName", &param.mName);
        csv->getValue(i, "NearLimitRate", &param.mNearLimitRate);
        csv->getValue(i, "FarLimitRate", &param.mFarLimitRate);
        csv->getValue(i, "EndLocalDirX", &param.mEndLocalDir.x);
        csv->getValue(i, "EndLocalDirY", &param.mEndLocalDir.y);
        csv->getValue(i, "EndLocalDirZ", &param.mEndLocalDir.z);
        csv->getValue(i, "EndDirMaxDegree", &param.mEndDirMaxDegree);
        holder->addCtrl(param);
    }

    return holder;
}
