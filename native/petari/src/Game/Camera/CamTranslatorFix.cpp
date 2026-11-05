#include "Game/Camera/CamTranslatorFix.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraParamChunk.hpp"

void CamTranslatorFix::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    Vec3f offset;
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &offset, general->mDist, general->mAxis.x, general->mAxis.y);

    mCamera->setParam(general->mWPoint, -offset, general->mUp);
}

Camera* CamTranslatorFix::getCamera() const {
    return mCamera;
}
