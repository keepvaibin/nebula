#include "Game/Camera/CamTranslatorCharmedTripodBoss.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Util/MathUtil.hpp"

void CamTranslatorCharmedTripodBoss::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mNum1, CameraLocalUtil::getNormalizedOr(general->mUp, Vec3f(0.0f, 1.0f, 0.0f)), general->mWPoint, Vec2f(general->mAxis.x, general->mAxis.y));
}

Camera* CamTranslatorCharmedTripodBoss::getCamera() const {
    return mCamera;
}
