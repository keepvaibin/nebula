#include "Game/Camera/CamTranslatorInnerCylinder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Util/MathUtil.hpp"

void CamTranslatorInnerCylinder::setParam(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;

    mCamera->setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(0.0f, 0.0f, 1.0f)), Vec2f(general->mAngleA, general->mAngleB), general->mDist, general->mUp.x);
}

Camera* CamTranslatorInnerCylinder::getCamera() const {
    return mCamera;
}
