#include "Game/Camera/CameraTrundle.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"

CameraTrundle::CameraTrundle(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(1.0f, 0.0f, 0.0f), mDist(2000.0f), mAngleX(), mAngleY(), mDotMax() {
}

void CameraTrundle::reset() {
}

CameraTargetObj* CameraTrundle::calc() {
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    Vec3f watchPoint = mWPoint;
    mZoneMatrix.mult(watchPoint, watchPoint);

    Vec3f side = mAxis;
    mZoneMatrix.mult33(side);

    Vec3f up = CameraLocalUtil::getTarget(this)->getPosition() - watchPoint;
    up.sub(side * side.dot(up));

    Vec3f camZ = up.cross(side);
    if (MR::isNearZero(camZ)) {
        return CameraLocalUtil::getTarget(this);
    }
    MR::normalize(&camZ);

    Quat4f rotX;
    rotX.setRotate(side, -mAngleX);
    rotX.transform(camZ);

    up.cross(side, camZ);

    if (!MR::isNearZero(mDotMax)) {
        f32 rate = (CameraLocalUtil::getTarget(this)->getPosition() - watchPoint).dot(side) / mDotMax;
        if (rate > 1.0f) {
            rate = 1.0f;
        }
        if (rate < -1.0f) {
            rate = -1.0f;
        }
        Quat4f rotY;
        rotY.setRotate(up, mAngleY * rate);
        rotY.transform(camZ);
    }

    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
    CameraLocalUtil::setPos(this, watchPos - camZ * mDist);
    CameraLocalUtil::setUpVec(this, up);

    return CameraLocalUtil::getTarget(this);
}

void CameraTrundle::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    Vec3f axis = general->mAxis;
    if (MR::isNearZero(axis)) {
        return;
    }
    MR::normalize(&axis);
    setParam(general->mWPoint, axis, general->mDist, general->mAngleA, general->mAngleB, general->mUp.x);
}
