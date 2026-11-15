#include "Game/Camera/CameraFixedPoint.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraFixedPoint::CameraFixedPoint(const char* pName) : Camera(pName), mPos(0.0f, 0.0f, 0.0f), mCameraType(CameraType_ZoneUp) {
    lumacore::setClass(this, lumacore::kClassId< CameraFixedPoint >);
}

void CameraFixedPoint::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getWatchUpVec(mCameraMan));
    calc();
}

CameraTargetObj* CameraFixedPoint::calc() {
    Vec3f prevWatchDir = CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this);
    MR::normalize(&prevWatchDir);

    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);
    CameraLocalUtil::setWatchPos(this, watchPos);

    Vec3f pos = mPos;
    mZoneMatrix.mult(pos, pos);
    CameraLocalUtil::setPos(this, pos);

    Vec3f watchDir = CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this);
    MR::normalize(&watchDir);

    switch (mCameraType) {
    case CameraType_ZoneUp: {
        Vec3f up(0.0f, 1.0f, 0.0f);
        mZoneMatrix.mult33(up, up);
        CameraLocalUtil::setUpVec(this, up);
        break;
    }
    case CameraType_TurnUp: {
        Quat4f rot;
        rot.setRotate(prevWatchDir, watchDir);
        Vec3f up = CameraLocalUtil::getUpVec(this);
        rot.transform(up);
        CameraLocalUtil::setUpVec(this, up);
        break;
    }
    case CameraType_PlayerUp: {
        Vec3f up;
        MR::getPlayerUpVec(&up);
        CameraLocalUtil::setUpVec(this, up);
        break;
    }
    }

    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
    return CameraLocalUtil::getTarget(this);
}

void CameraFixedPoint::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, general->mNum1);
}

void CameraFixedPoint::setParam(const Vec3f& rPos, u32 cameraType) {
    mPos.set(rPos);
    mCameraType = cameraType;
}
