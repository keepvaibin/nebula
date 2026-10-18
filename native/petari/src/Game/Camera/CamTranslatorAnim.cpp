#include "Game/Camera/CamTranslatorAnim.hpp"
#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraParamChunk.hpp"

void CamTranslatorAnim::setParam(const CameraParamChunk* pChunk) {
    CameraAnim* camera = mCamera;

    // mNum1 is used to store the pointer to the data
    #ifdef TARGET_PC
    camera->setParam(pChunk->mGeneralParam->mAnimData, pChunk->mGeneralParam->mDist);
#else
    camera->setParam(reinterpret_cast< u8* >(pChunk->mGeneralParam->mNum1), pChunk->mGeneralParam->mDist);
#endif
}

Camera* CamTranslatorAnim::CamTranslatorAnim::getCamera() const {
    return mCamera;
}

u32 CamTranslatorAnim::getAnimFrame(const CameraParamChunk* pChunk) const {
#ifdef TARGET_PC
    return CameraAnim::getAnimFrame(pChunk->mGeneralParam->mAnimData);
#else
    return CameraAnim::getAnimFrame(reinterpret_cast< u8* >(pChunk->mGeneralParam->mNum1));
#endif
}