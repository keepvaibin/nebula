#include "Game/Camera/CameraTrundle.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);

CameraTrundle::CameraTrundle(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(1.0f, 0.0f, 0.0f), mDist(2000.0f), mAngleX(), mAngleY(), mDotMax() {
}

CameraTargetObj* CameraTrundle::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, target, cNum_calc_f1 / cNum_calc_f2);

    Vec3f watchPoint = mWPoint;
    mZoneMatrix.mult(watchPoint, watchPoint);

    Vec3f side = mAxis;
    mZoneMatrix.mult33(side);

    Vec3f up = target->getPosition() - watchPoint;
    up.sub(side * side.dot(up));

    Vec3f camZ = up.cross(side);
    if (MR::isNearZero(camZ)) {
        return target;
    }
    MR::normalize(&camZ);

    Quat4f rotX;
    rotX.setRotate(side, -mAngleX);
    rotX.transform(camZ);

    up.cross(side, camZ);

    if (!MR::isNearZero(mDotMax)) {
        f32 rate = (target->getPosition() - watchPoint).dot(side) / mDotMax;
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
    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());
    CameraLocalUtil::setPos(this, watchPos - camZ * mDist);
    CameraLocalUtil::setUpVec(this, up);

    return target;
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
