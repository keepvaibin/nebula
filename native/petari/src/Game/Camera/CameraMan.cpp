#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/System/LumaCore.hpp"

CameraMan::CameraMan(const char* pName)
    : NameObj(pName), mDirector(nullptr), mPoseParam(new CameraPoseParam()), mIsActivated(false), mRequestLOfsReset(false) {
    lumacore::setClass(this, lumacore::kClassId< CameraMan >);
    mMatrix.identity();
}

void CameraMan::owned(CameraDirector* pDirector) {
    mDirector = pDirector;
}

void CameraMan::released() {
    mDirector = nullptr;
}

void CameraMan::activate() {
    mIsActivated = true;
    notifyActivate();
}

void CameraMan::deactivate() {
    mIsActivated = false;
    notifyDeactivate();
}

void CameraMan::movement() {
    if (mIsActivated) {
        calc();
        mRequestLOfsReset = false;
    }
}
