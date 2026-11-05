#include "Game/Boss/SkeletalFishJointCalc.hpp"
#include "Game/Boss/SkeletalFishRailControl.hpp"
#include "Game/Util/JointController.hpp"

void SkeletalFishJointCalc::calcJoint(Pos3f* pDst, const Pos3f* pSrc, SkeletalFishRailControl* pRailCtrl, const JointControllerInfo&) {
    pDst->concat(*pSrc, *pDst);

    Vec3f trans;
    pDst->getTrans(trans);

    Pos3f offsetMtx;
    offsetMtx.makeTrans(0.0f, 0.0f, -trans.z);
    pDst->concat(offsetMtx, *pDst);

    Pos3f fishRailControlMtx;
    pRailCtrl->getMtx(&fishRailControlMtx, -trans.z);
    pDst->concat(fishRailControlMtx, *pDst);
}
