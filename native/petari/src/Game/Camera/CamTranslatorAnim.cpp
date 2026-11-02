#include "Game/Camera/CamTranslatorAnim.hpp"
#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraParamChunk.hpp"

void CamTranslatorAnim::setParam(const CameraParamChunk* pChunk) {
    CameraAnim* camera = mCamera;

    // mNum1 is used to store the pointer to the data
    camera->setParam(pChunk->mGeneralParam->mAnimData, pChunk->mGeneralParam->mDist);
}

Camera* CamTranslatorAnim::CamTranslatorAnim::getCamera() const {
    return mCamera;
}

u32 CamTranslatorAnim::getAnimFrame(const CameraParamChunk* pChunk) const {
    return CameraAnim::getAnimFrame(pChunk->mGeneralParam->mAnimData);
}