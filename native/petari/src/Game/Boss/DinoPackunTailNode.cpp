#include "Game/Boss/DinoPackunTailNode.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/System/LumaCore.hpp"

typedef JointControlDelegator< DinoPackunTailNode > Delegator;

typedef bool (DinoPackunTailNode::*func)(Pos3f*, const JointControllerInfo&);

DinoPackunTailNode::DinoPackunTailNode(const char* pName, DinoPackun* pParent)
    : LiveActor(pName), mParent(pParent), mNodeDirection(0.0f, 0.0f, 0.0f), mJointPos(0.0f, 0.0f, 0.0f), mJointFront(0.0f, 0.0f, 0.0f),
      mPrevNode(nullptr), mNextNode(nullptr), mJointMtx(nullptr), mPreCalcJointCtrl(nullptr), mJointCtrl(nullptr), mLinkLength(90.0f),
      mKeepBendPower(10.0f), mIsLocked(false) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunTailNode >);
}

void DinoPackunTailNode::createJointController(LiveActor* pHost, const char* pJointName) {
    mJointCtrl = static_cast< Delegator* >(createJointControllerOwn(pHost, pJointName));
    mPreCalcJointCtrl = static_cast< Delegator* >(MR::createJointDelegator(this, pHost, &DinoPackunTailNode::preCalcJoint, (func) nullptr, pJointName));
    mJointMtx = MR::getJointMtx(pHost, pJointName);
    resetJoint();
}

void DinoPackunTailNode::resetJoint() {
    Pos3f pos(mJointMtx);
    pos.getXDir(mNodeDirection);
    pos.getTrans(mPosition);
    MR::zeroVelocity(this);
}

f32 DinoPackunTailNode::getLinkLength() const {
    return mLinkLength;
}

f32 DinoPackunTailNode::getKeepBendPower() const {
    return mKeepBendPower;
}

bool DinoPackunTailNode::preCalcJoint(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    pMtx->getTrans(mJointPos);
    return false;
}

bool DinoPackunTailNode::turnJointLocalXDir(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    if (!mPrevNode && !mNextNode) {
        return false;
    }

    // Turns the joint's X axis onto the link toward the next node (from the previous one at the tip).
    Pos3f jointMtx(pMtx);
    MR::orthogonalize(&jointMtx);
    jointMtx.getXDir(mNodeDirection);
    jointMtx.getZDir(mJointFront);
    MR::normalize(&mNodeDirection);
    Quat4f jointQuat;
    jointMtx.getQuat(jointQuat);

    Vec3f linkDir;
    if (mNextNode != nullptr) {
        linkDir.set(mNextNode->mPosition - mPosition);
    } else {
        linkDir.set(mPosition - mPrevNode->mPosition);
    }

    if (MR::isNearZero(linkDir)) {
        return false;
    }

    MR::normalize(&linkDir);
    MR::turnQuatXDirRad(&jointQuat, jointQuat, linkDir, M_PI);
    pMtx->setQuat(jointQuat);
    pMtx->setTrans(mPosition);
    return true;
}

bool DinoPackunTailNode::calcJointScale(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    if (!mNextNode) {
        return false;
    }

    // Stretches along the link and thins to keep the volume.
    f32 distance = mNextNode->mPosition.distance(mPosition);
    f32 stretch = MR::clamp(distance / mLinkLength, 0.2f, 5.0f);
    f32 shrink = MR::sqrt(1.0f / stretch);
    MR::preScaleMtx(*pMtx, stretch, shrink, shrink);
    return true;
}

void DinoPackunTailNode::registerPreCalcJointCallBack() {
    mPreCalcJointCtrl->registerCallBack();
}

void DinoPackunTailNode::registerJointCallBack() {
    mJointCtrl->registerCallBack();
}

void DinoPackunTailNode::lockPosition() {
    if (!mIsLocked) {
        requestLockPosition();
        mIsLocked = true;
    }
}

void DinoPackunTailNode::unLockPosition() {
    if (mIsLocked) {
        requestUnLockPosition();
        mIsLocked = false;
    }
}

void DinoPackunTailNode::addNodeVelocityHost(const Vec3f& rVelocity) {
    if (!mIsLocked) {
        addNodeVelocity(rVelocity);
    }
}

const Vec3f* DinoPackunTailNode::getNodeDirection() const {
    return &mNodeDirection;
}

void DinoPackunTailNode::addNodeVelocity(const Vec3f& rVel) {
    mVelocity.add(rVel);
}

void DinoPackunTailNode::requestLockPosition() {}

void DinoPackunTailNode::requestUnLockPosition() {}
