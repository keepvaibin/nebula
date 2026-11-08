#include "Game/Boss/DinoPackunTailNode.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

typedef JointControlDelegator< DinoPackunTailNode > Delegator;

typedef bool (DinoPackunTailNode::*func)(Pos3f*, const JointControllerInfo&);

DinoPackunTailNode::DinoPackunTailNode(const char* pName, DinoPackun* pParent)
    : LiveActor(pName), mParent(pParent), mNodeDirection(0, 0, 0), mJointPos(0, 0, 0), mJointFront(0.0f, 0.0f, 0.0f), mPrevNode(), mNextNode(), mJointMtx(), mPreCalcJointCtrl(), mJointCtrl(),
      mLinkLength(90.0f), mKeepBendPower(10.0f), mIsLocked() {
}

void DinoPackunTailNode::createJointController(LiveActor* pHost, const char* pJointName) {
    mJointCtrl = static_cast< Delegator* >(createJointControllerOwn(pHost, pJointName));
    JointController* cnt = MR::createJointDelegator(this, pHost, &DinoPackunTailNode::preCalcJoint, (func) nullptr, pJointName);
    mPreCalcJointCtrl = static_cast< Delegator* >(cnt);
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

bool DinoPackunTailNode::preCalcJoint(Pos3f* pMtx, const JointControllerInfo& rInfo) {
    pMtx->getTrans(mJointPos);
    return false;
}

bool DinoPackunTailNode::turnJointLocalXDir(Pos3f* pMtx, const JointControllerInfo& rInfo) {
    if (!mPrevNode && !mNextNode) {
        return false;
    }

    Pos3f v25(pMtx);
    MR::orthogonalize(&v25);
    v25.getXDir(mNodeDirection);
    v25.getZDir(mJointFront);
    MR::normalize(&mNodeDirection);
    Quat4f v24;
    v25.getQuat(v24);

    Vec3f v23;

    if (mNextNode) {
        Vec3f v22;
        v22.set(mNextNode->mPosition);
        v23.set(v22 - mPosition);
    } else {
        Vec3f v21;
        v21.set(mPrevNode->mPosition);
        v23.set(mPosition - v21);
    }

    if (MR::isNearZero(v23)) {
        return false;
    }

    MR::normalize(&v23);
    MR::turnQuatXDirRad(&v24, v24, v23, M_PI);
    pMtx->setQuat(v24);
    pMtx->setTrans(mPosition);
    return true;
}

bool DinoPackunTailNode::calcJointScale(Pos3f* pMtx, const JointControllerInfo& rInfo) {
    if (!mNextNode) {
        return false;
    }

    Vec3f v12;
    v12.set(mNextNode->mPosition);
    f32 v6 = v12.distance(mPosition);
    f32 stretch = MR::clamp(v6 / mLinkLength, 0.2f, 5.0f);
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
        mIsLocked = 1;
    }
}

void DinoPackunTailNode::unLockPosition() {
    if (mIsLocked) {
        requestUnLockPosition();
        mIsLocked = 0;
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

void DinoPackunTailNode::requestLockPosition() {
}

void DinoPackunTailNode::requestUnLockPosition() {
}
