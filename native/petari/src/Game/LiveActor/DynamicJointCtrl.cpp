#include "Game/LiveActor/DynamicJointCtrl.hpp"
#include "Game/Util.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

namespace {
    DynamicJointCtrlParam sDefaultParam = {0.3f, 0.98f, 0.01f, 45.0f, -1};
    NEBULA_TUNABLE(s32, sDefaultCtrlTime, 10);
    const char* sTextOutFileName = "DynamicJointCtrl";

    s32 calcJointChildNum(Joint* pJoint) {
        if (pJoint->mChild == nullptr) {
            return 0;
        } else {
            return calcJointChildNum(pJoint->mChild) + 1;
        }
    }
};  // namespace

JointCtrlRate::JointCtrlRate() : mRate(1.0f), mTime(), mStartTime(-1), mEndTime(-1) {
}

void JointCtrlRate::update() {
    if (mStartTime >= 0) {
        mTime--;
        if (mTime < 0) {
            mStartTime = -1;
            mRate = 1.0f;
        } else {
            mRate = static_cast< f32 >(mStartTime - mTime) / static_cast< f32 >(mStartTime);
        }
    } else if (mEndTime >= 0) {
        mTime--;
        if (mTime < 0) {
            mEndTime = -1;
            mRate = 0.0f;
        } else {
            mRate = static_cast< f32 >(mTime) / static_cast< f32 >(mEndTime);
        }
    }
}

void JointCtrlRate::startCtrl(s32 time) {
    if (time < 0) {
        time = ::sDefaultCtrlTime;
    }

    mStartTime = time;
    mEndTime = -1;
    mTime = time;
    mRate = 0.0f;
}

void JointCtrlRate::endCtrl(s32 time) {
    if (time < 0) {
        time = ::sDefaultCtrlTime;
    }

    mEndTime = time;
    mStartTime = -1;
    mTime = time;
    mRate = 1.0f;
}

DynamicJointCtrlNode::DynamicJointCtrlNode(const DynamicJointCtrl* pControl, MtxPtr pMtx, f32 length)
    : mLength(length), mPos(0.0f, 0.0f, 0.0f), mVelocity(0.0f, 0.0f, 0.0f), mDir(0.0f, 0.0f, 1.0f), mMtx(pMtx), mJointController(), mChild(),
      mParentControl(pControl) {
    if (pMtx) {
        mPos.set< f32 >(pMtx[0][3], pMtx[1][3], pMtx[2][3]);
        mDir.set< f32 >(pMtx[0][0], pMtx[1][0], pMtx[2][0]);
    }
}

void DynamicJointCtrlNode::update(LiveActor* pActor, const DynamicJointCtrlNode* pParent) {
    mVelocity.scale(mParentControl->mParams->mFriction);
    Vec3f gravity(0.0f, 0.0f, 0.0f);
    MR::calcGravityVector(pActor, &gravity, nullptr, 0);
    gravity.scale(mParentControl->mParams->mGravity);
    mVelocity.add(gravity);

    Vec3f targetPos(pParent->mDir);
    targetPos.scale(mLength);
    targetPos.add(pParent->mPos);
    Vec3f accel(targetPos);
    accel.sub(mPos);
    accel.sub(mVelocity);
    accel.scale(mParentControl->mParams->mAccelRatetoBckPos);
    mVelocity.add(accel);

    Vec3f offset(mPos);
    offset.add(mVelocity);
    offset.sub(pParent->mPos);
    Vec3f direction(offset);

    if (MR::isNearZero(direction)) {
        direction.set(pParent->mDir);
    } else {
        MR::normalize(&direction);
    }

    if (offset.length() >= mLength) {
        Vec3f limitedOffset(direction);
        limitedOffset.scale(mLength);
        Vec3f correction(offset);
        correction.sub(limitedOffset);
        mVelocity.sub(correction);
    }

    mPos.add(mVelocity);
}

void DynamicJointCtrlNode::reset() {
    mPos.set< f32 >(mMtx[0][3], mMtx[1][3], mMtx[2][3]);
    mVelocity.zero();
}

bool DynamicJointCtrlNode::updateJointMtxCallBack(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    Vec3f axis;
    pMtx->getXDir(axis);

    if (MR::isNearZero(axis)) {
        return false;
    }

    mDir.set(axis);
    MR::normalize(&mDir);

    f32 rate = mParentControl->mControlRate->mRate;
    if (rate == 0.0f) {
        return false;
    }

    Vec3f direction(mChild->mPos);
    direction.sub(mPos);

    if (MR::isNearZero(direction)) {
        return false;
    }

    MR::normalize(&direction);

    f32 blendRate = mParentControl->mControlRate->mRate;
    MR::turnMtxToXDirDegree(pMtx, direction, mParentControl->mParams->mBendMaxDegree * blendRate);
    return true;
}

DynamicJointCtrl::DynamicJointCtrl(LiveActor* pActor, const char* pName, const DynamicJointCtrlParam* pParam)
    : mActor(pActor), mName(pName), mNodeNum(), mCtrlNodes(), mParams(pParam), mControlRate(new JointCtrlRate) {
}

void DynamicJointCtrl::init() {
    MR::getJointMtx(mActor, mName);
    Joint* joint = MR::getJoint(mActor, mName);
    // The joint chain below the first joint gives one node a joint plus the free end: a count from the file past it (or under
    // the two the chain needs) is held to that.
    const s32 chainNodeNum = ::calcJointChildNum(joint) + 2;
    if (mParams->mCtrlJointNumMax > 0) {
        mNodeNum = mParams->mCtrlJointNumMax < 2 ? 2 : mParams->mCtrlJointNumMax;
        if (mNodeNum > chainNodeNum) {
            mNodeNum = chainNodeNum;
        }
    } else {
        mNodeNum = chainNodeNum;
    }

    mCtrlNodes = new DynamicJointCtrlNode*[mNodeNum];
    Vec3f previousPos(0.0f, 0.0f, 0.0f);
    f32 length = cNum_init_f1;

    for (s32 i = 0; i < mNodeNum; i++) {
        MtxPtr mtx = nullptr;
        if (i < mNodeNum - 1) {
            mtx = MR::getJointMtx(mActor, joint->getJntNo());
        }

        if (i < mNodeNum - 1) {
            Vec3f pos(mtx[0][3], mtx[1][3], mtx[2][3]);
            if (i > 0) {
                length = pos.distance(previousPos);
            }

            previousPos.set(pos);
        }

        mCtrlNodes[i] = new DynamicJointCtrlNode(this, mtx, length);
        if (i < mNodeNum - 1) {
            const char* name = MR::getJointName(mActor, joint->getJntNo());
            mCtrlNodes[i]->mJointController =
                MR::createJointDelegator< DynamicJointCtrlNode >(mCtrlNodes[i], mActor, &DynamicJointCtrlNode::updateJointMtxCallBack, nullptr, name);
            joint = joint->mChild;
        }
    }

    for (s32 i = 0; i < mNodeNum - 1; i++) {
        mCtrlNodes[i]->mChild = mCtrlNodes[i + 1];
    }
}

void DynamicJointCtrl::update() {
    mCtrlNodes[0]->reset();

    for (s32 i = 1; i < mNodeNum; i++) {
        mCtrlNodes[i]->update(mActor, mCtrlNodes[i - 1]);
    }

    mControlRate->update();
}

void DynamicJointCtrl::reset() {
    for (s32 i = 0; i < mNodeNum - 1; i++) {
        mCtrlNodes[i]->reset();
    }

    int endIndex = mNodeNum - 1;
    int parentIndex = mNodeNum - 2;
    mCtrlNodes[endIndex]->update(mActor, mCtrlNodes[parentIndex]);
}

void DynamicJointCtrl::setCallBackFunction() {
    for (s32 i = 0; i < mNodeNum - 1; i++) {
        mCtrlNodes[i]->mJointController->registerCallBack();
    }
}

DynamicJointCtrlKeeper::DynamicJointCtrlKeeper(LiveActor* pActor) : mActor(pActor), mControlNum(), mControls() {
    const ResourceHolder* resourceHolder = MR::getResourceHolder(pActor);
    Table* csv = MR::tryCreateCsvParser(resourceHolder, "%s.bcsv", ::sTextOutFileName);
    if (!csv) {
        return;
    }

    mControlNum = csv->getNumEntries();
    mControls = new DynamicJointCtrl*[mControlNum];

    for (s32 i = 0; i < mControlNum; i++) {
        DynamicJointCtrlParam* param = new DynamicJointCtrlParam;
        *param = ::sDefaultParam;
        const char* name = nullptr;
        csv->getValue(i, "JointName", &name);
        csv->getValue(i, "Gravity", &param->mGravity);
        csv->getValue(i, "Friction", &param->mFriction);
        csv->getValue(i, "AccelRateToBckPos", &param->mAccelRatetoBckPos);
        csv->getValue(i, "BendMaxDegree", &param->mBendMaxDegree);
        csv->getValue(i, "CtrlJointNumMax", &param->mCtrlJointNumMax);
        mControls[i] = new DynamicJointCtrl(pActor, name, param);
        mControls[i]->init();
    }
}

void DynamicJointCtrlKeeper::update() {
    for (s32 i = 0; i < mControlNum; i++) {
        mControls[i]->update();
    }
}

void DynamicJointCtrlKeeper::setCallBackFunction() {
    for (s32 i = 0; i < mControlNum; i++) {
        mControls[i]->setCallBackFunction();
    }
}

void DynamicJointCtrlKeeper::startCtrl(const char* pName, s32 time) {
    if (DynamicJointCtrl* const pCtrl = findJointCtrl(pName)) {
        pCtrl->mControlRate->startCtrl(time);
    }
}

void DynamicJointCtrlKeeper::endCtrl(const char* pName, s32 time) {
    if (DynamicJointCtrl* const pCtrl = findJointCtrl(pName)) {
        pCtrl->mControlRate->endCtrl(time);
    }
}

void DynamicJointCtrlKeeper::reset() {
    for (s32 i = 0; i < mControlNum; i++) {
        mControls[i]->reset();
    }
}

DynamicJointCtrl* DynamicJointCtrlKeeper::findJointCtrl(const char* pName) {
    for (s32 i = 0; i < mControlNum; i++) {
        if (MR::isEqualString(mControls[i]->mName, pName)) {
            return mControls[i];
        }
    }

    // An unknown name takes the first control; with none (no table) there is nothing to find.
    return mControlNum > 0 ? mControls[0] : nullptr;
}
