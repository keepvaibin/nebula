#include "Game/Camera/CamTranslatorTripodBoss.hpp"
#include "Game/Camera/CameraParamChunk.hpp"

void CamTranslatorTripodBoss::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mWPoint, general->mAxis, general->mDist, Vec2f(general->mUp.x, general->mUp.y));
    mCamera->mAngleY = general->mUp.z;
}

Camera* CamTranslatorTripodBoss::getCamera() const {
    return mCamera;
}
