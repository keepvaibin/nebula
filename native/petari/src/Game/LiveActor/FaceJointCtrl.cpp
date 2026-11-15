#include "Game/LiveActor/FaceJointCtrl.hpp"
#include "Game/LiveActor/DynamicJointCtrl.hpp"
#include "Game/Util/JointController.hpp"

#include "Game/Util.hpp"

namespace {
    const f32 sDefaultDegreeMax = 45.0f;
    const char* sTextOutFileName = "FaceJointCtrl";
};  // namespace

FaceJointCtrl::FaceJointCtrl(LiveActor* pActor)
    : mHostActor(pActor), mDegreeMax(::sDefaultDegreeMax), mJointController() {
    mJointCtrlRate = new JointCtrlRate();
}

void FaceJointCtrl::init() {
    const ResourceHolder* resourceHolder = MR::getResourceHolder(mHostActor);
    Table* csv = MR::tryCreateCsvParser(resourceHolder, "%s.bcsv", ::sTextOutFileName);
    if (csv) {
        const char* jointName = nullptr;
        csv->getValue(0, "JointName", &jointName);
        csv->getValue(0, "DegreeMax", &mDegreeMax);
        // A table with no joint name has no joint to turn.
        if (jointName == nullptr) {
            return;
        }

        mJointController = MR::createJointDelegator< FaceJointCtrl >(this, mHostActor, &FaceJointCtrl::updateJointMtxCallBack, nullptr, jointName);
    }
}

void FaceJointCtrl::startCtrl(s32 time) {
    if (mJointController) {
        mJointCtrlRate->startCtrl(time);
        update();
    }
}

void FaceJointCtrl::endCtrl(s32 time) {
    if (mJointController) {
        mJointCtrlRate->endCtrl(time);
        update();
    }
}

void FaceJointCtrl::update() {
    if (!mJointController) {
        return;
    }

    mJointCtrlRate->update();
}

void FaceJointCtrl::setCallBackFunction() {
    if (!mJointController) {
        return;
    }

    mJointController->registerCallBack();
}

bool FaceJointCtrl::updateJointMtxCallBack(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    Vec3f position;
    pMtx->getTrans(position);
    Vec3f direction(*MR::getPlayerPos());
    direction.sub(position);

    if (MR::isNearZero(direction)) {
        return false;
    }

    MR::normalize(&direction);
    f32 rate = mJointCtrlRate->mRate;
    MR::turnMtxToXDirDegree(pMtx, direction, mDegreeMax * rate);
    return true;
}
