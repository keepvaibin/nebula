#include "Game/Camera/CamTranslatorCharmedFix.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Util/MathUtil.hpp"

void CamTranslatorCharmedFix::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mAxis, CameraLocalUtil::getNormalizedOr(general->mUp, Vec3f(0.0f, 1.0f, 0.0f)), general->mWPoint);
}

Camera* CamTranslatorCharmedFix::getCamera() const {
    return mCamera;
}
