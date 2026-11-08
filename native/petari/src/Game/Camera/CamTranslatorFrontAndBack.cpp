#include "Game/Camera/CamTranslatorFrontAndBack.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Util/MathUtil.hpp"

void CamTranslatorFrontAndBack::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(1.0f, 0.0f, 0.0f)), MR::pi() * general->mAngleA / 180.0f, MR::pi() * general->mAngleB / 180.0f, general->mDist);
}

Camera* CamTranslatorFrontAndBack::getCamera() const {
    return mCamera;
}
