#include "Game/Camera/CameraCharmedFix.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraCharmedFix::CameraCharmedFix(const char* pName)
    : Camera(pName), mBasePos(0.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mWPoint(0.0f, 0.0f, 1000.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraCharmedFix >);
}

void CameraCharmedFix::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
}

CameraTargetObj* CameraCharmedFix::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    const Pos3f& zoneMtx = mZoneMatrix;
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, target, 0.1f / 15.0f);

    Vec3f pos = mBasePos;
    zoneMtx.mult(pos, pos);
    Vec3f front = target->getPosition() - pos;
    if (MR::isNearZero(front)) {
        return target;
    }

    MR::normalize(&front);
    Vec3f up = mUp;
    zoneMtx.mult33(up, up);
    Vec3f side = up.cross(front);
    if (MR::isNearZero(side)) {
        return target;
    }

    MR::normalize(&side);
    up.cross(front, side);

    Pos3f mtx;
    mtx.setXYZDir(side, up, front);
    mtx.setTrans(target->getPosition());

    Vec3f watchPos = mWPoint;
    mtx.mult(watchPos, watchPos);
    watchPoint = watchPos - front * mWPoint.z;
    CameraLocalUtil::setPos(this, watchPos);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());

    return target;
}

void CameraCharmedFix::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis, CameraLocalUtil::getNormalizedOr(general->mUp, Vec3f(0.0f, 1.0f, 0.0f)), general->mWPoint);
}
