#include "Game/Camera/CamTranslatorPointer.hpp"
#include "Game/Camera/CameraParamChunk.hpp"

void CamTranslatorPointer::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mNum1, general->mDist, general->mAngleA, general->mAngleB, general->mWPoint.z, general->mWPoint.x, general->mWPoint.y,
                      general->mNum2 == CameraPointer::CameraType_UpdateWithTarget, general->mUp.x);
}

Camera* CamTranslatorPointer::getCamera() const {
    return mCamera;
}
