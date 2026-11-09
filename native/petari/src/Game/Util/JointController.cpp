#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"

JointController::JointController() : mModel(), mJoint() {
}

bool JointController::calcJointMatrix(Pos3f* /*pMtx*/, const JointControllerInfo& /*rInfo*/) {
    return false;
}

bool JointController::calcJointMatrixAfterChild(Pos3f* /*pMtx*/, const JointControllerInfo& /*rInfo*/) {
    return false;
}

void JointController::registerCallBack() {
    mJoint->setCallBack(staticCallBack);
    mJoint->mCallBackUserData = this;
}

void JointController::calcJointMatrixAndSetSystem(Joint* pJoint) {
    MtxPtr pJointMtx = mModel->getAnmMtx(pJoint->getJntNo());
    Pos3f mtx;
    NebulaMtxCopy(pJointMtx, mtx);
    JointControllerInfo info = {this, pJoint};

    if (calcJointMatrix(&mtx, info)) {
        NebulaMtxCopy(mtx, pJointMtx);
        NebulaMtxCopy(mtx, Skeleton::mCurrentMtx);
    }
}

void JointController::calcJointMatrixAfterChildAndSetSystem(Joint* pJoint) {
    MtxPtr pJointMtx = mModel->getAnmMtx(pJoint->getJntNo());
    Pos3f mtx;
    NebulaMtxCopy(pJointMtx, mtx);
    JointControllerInfo info = {this, pJoint};

    if (calcJointMatrixAfterChild(&mtx, info)) {
        NebulaMtxCopy(mtx, pJointMtx);
    }
}

int JointController::staticCallBack(Joint* pJoint, int timing) {
    if (!pJoint) {
        return 0;
    }

    JointController* pController = static_cast< JointController* >(pJoint->mCallBackUserData);
    if (!pController) {
        return 0;
    }

    if (timing == 0) {
        pController->calcJointMatrixAndSetSystem(pJoint);
    }

    if (timing == 1) {
        pController->calcJointMatrixAfterChildAndSetSystem(pJoint);
        pJoint->setCallBack(nullptr);
        pJoint->mCallBackUserData = nullptr;
    }

    return 0;
}

namespace MR {
    void setJointControllerParam(JointController* pController, const LiveActor* pActor, const char* pJointName) {
        Joint* pJoint = getJoint(pActor, pJointName);
        pController->mModel = getModel(pActor);
        pController->mJoint = pJoint;
    }

    void setJointControllerParam(JointController* pController, const LiveActor* pActor, u32 jointIndex) {
        Joint* pJoint = getJoint(pActor, jointIndex);
        pController->mModel = getModel(pActor);
        pController->mJoint = pJoint;
    }
};  // namespace MR
