#include "Game/Camera/CamTranslatorSpiral.hpp"
#include "Game/Camera/CameraParamChunk.hpp"

void CamTranslatorSpiral::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    s32 startTime = (s16)((u32)general->mNum1 >> 16);
    s32 endTime = (s16)((u32)general->mNum1 & 0xFFFF);

    mCamera->setParam(general->mNum2, startTime, endTime, general->mWPoint.y, general->mAxis.y, general->mWPoint.z, general->mAxis.z,
                      general->mWPoint.x, general->mAxis.x);
}

Camera* CamTranslatorSpiral::getCamera() const {
    return mCamera;
}
