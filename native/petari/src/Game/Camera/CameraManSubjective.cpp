#include "Game/Camera/CameraManSubjective.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraSubjective.hpp"
#include "Game/System/LumaCore.hpp"

CameraManSubjective::CameraManSubjective(const char* pName) : CameraMan(pName), mCamera(new CameraSubjective("主観カメラ")) {
    lumacore::setClass(this, lumacore::kClassId< CameraManSubjective >);
    mCamera->mCameraMan = this;
}

void CameraManSubjective::calc() {
    mCamera->calc();
    CameraLocalUtil::calcSafePose(this, mCamera);
}

void CameraManSubjective::notifyActivate() {
    mCamera->reset();
}
