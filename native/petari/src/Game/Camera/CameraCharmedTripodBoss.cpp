#include "Game/Camera/CameraCharmedTripodBoss.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);

CameraCharmedTripodBoss::CameraCharmedTripodBoss(const char* pName)
    : Camera(pName), mJointId(-1), mUp(0.0f, 1.0f, 0.0f), mWPoint(0.0f, 0.0f, 1000.0f), mAngleX(), mAngleY() {
    lumacore::setClass(this, lumacore::kClassId< CameraCharmedTripodBoss >);
}

void CameraCharmedTripodBoss::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
}

CameraTargetObj* CameraCharmedTripodBoss::calc() {
    CameraTargetObj* obj = CameraLocalUtil::getTarget(this);
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, obj, cNum_calc_f1 / cNum_calc_f2);

    Vec3f up = mUp;
    Vec3f jointPos(0.0f, 0.0f, 0.0f);

    if (mJointId >= 0) {
        Pos3f jointMtx;
        MR::getTripodBossJointMatrix(&jointMtx, mJointId);
        jointMtx.getTrans(jointPos);
        jointMtx.mult33(up, up);
    }

    Pos3f rotX;
    rotX.makeRotate(Vec3f(-1.0f, 0.0f, 0.0f), mAngleX);
    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngleY);

    Pos3f rotMtx;
    rotMtx.concat(rotY, rotX);

    Vec3f front = obj->getPosition() - jointPos;
    if (MR::isNearZero(front)) {
        return obj;
    }
    MR::normalize(&front);

    Vec3f side = up.cross(front);
    if (MR::isNearZero(side)) {
        return obj;
    }
    MR::normalize(&side);

    up.cross(front, side);
    MR::normalize(&front);

    Pos3f mtx;
    mtx.setXYZDir(side, up, front);
    mtx.setTrans(obj->getPosition());
    mtx.concat(mtx, rotMtx);
    mtx.getZDir(front);
    mtx.getYDir(up);

    Vec3f pos = mWPoint;
    mtx.mult(pos, pos);

    watchPos = pos - front * mWPoint.z;
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, mUp);

    return obj;
}

void CameraCharmedTripodBoss::setParam(s32 jointID, Vec3f up, const Vec3f& wPoint, const Vec2f& angle) {
    mAngleX = angle.x;
    mAngleY = angle.y;
    mUp.set(up);
    mWPoint.set(wPoint);
    mJointId = jointID;
}

void CameraCharmedTripodBoss::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mNum1, CameraLocalUtil::getNormalizedOr(general->mUp, Vec3f(0.0f, 1.0f, 0.0f)), general->mWPoint, Vec2f(general->mAxis.x, general->mAxis.y));
}
