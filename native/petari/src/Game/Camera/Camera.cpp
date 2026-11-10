#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/LumaCore.hpp"

Camera::Camera(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< Camera >);
    mPoseParam = new CameraPoseParam();
    mVPan = nullptr;
    mIsLOfsErpOff = false;
    mZoneMatrix.identity();
}

void Camera::setZoneMtx(s32 zoneID) {
    if (zoneID <= 0) {
        mZoneMatrix.identity();
    } else {
        mZoneMatrix.set(*MR::getZonePlacementMtx(zoneID));
    }

    if (doesVPanExist()) {
        mVPan->mUpdateGlobalAxis = true;
    }
}

void Camera::createVPanObj() {
    mVPan = new CameraHeightArrange(this);
}
