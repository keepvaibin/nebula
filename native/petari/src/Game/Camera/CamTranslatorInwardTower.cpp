#include "Game/Camera/CamTranslatorInwardTower.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Util/MathUtil.hpp"

void CamTranslatorInwardTower::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(0.0f, 1.0f, 0.0f)), Vec2f(general->mAngleB, general->mAngleA), general->mDist);
}

Camera* CamTranslatorInwardTower::getCamera() const {
    return mCamera;
}
