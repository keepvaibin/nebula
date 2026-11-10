#include "Game/Camera/CameraFrontAndBack.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setParamFromChunk_f1, "setParamFromChunk.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setParamFromChunk_f2, "setParamFromChunk.f2", 180.0f);

namespace {
    NEBULA_TUNABLE(s32, sInterpolation, 120);
    NEBULA_TUNABLE(f32, sWarpDistance, 500.0f);
};  // namespace

CameraFrontAndBack::CameraFrontAndBack(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 1.0f, 0.0f), mRotateRate(), mMaxDipAngle(), mDist(1200.0f) {
}

CameraTargetObj* CameraFrontAndBack::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    Vec3f pos = CameraLocalUtil::getPos(this);
    Vec3f axis = mAxis;
    mZoneMatrix.mult33(axis);
    Vec3f wPoint = mWPoint;
    mZoneMatrix.mult(wPoint, wPoint);

    Vec3f posDir = target->getPosition() - wPoint;
    posDir.orthogonalize(axis);
    if (MR::isNearZero(posDir)) {
        return nullptr;
    }

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, target, cNum_calc_f1 / cNum_calc_f2);
    Vec3f front, up;
    mZoneMatrix.getZDir(front);
    mZoneMatrix.getYDir(up);

    Quat4f baseQuat;
    calcBaseQuat(&baseQuat, axis);
    baseQuat.transform(front);
    baseQuat.transform(up);

    Quat4f dipQuat;
    calcDipQuat(&dipQuat, front, posDir);
    dipQuat.transform(front);
    dipQuat.transform(up);

    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setPos(this, target->getPosition() + front * mDist);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
    if (pos.distance(CameraLocalUtil::getPos(this)) > ::sWarpDistance) {
        MR::startCameraInterpolation(::sInterpolation);
    }

    return target;
}

void CameraFrontAndBack::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(1.0f, 0.0f, 0.0f)), MR::pi() * general->mAngleA / cNum_setParamFromChunk_f1, MR::pi() * general->mAngleB / cNum_setParamFromChunk_f2, general->mDist);
}

void CameraFrontAndBack::calcBaseQuat(Quat4f* pQuat, const Vec3f& rAxis) {
    Vec3f side;
    mZoneMatrix.getXDir(side);
    Quat4f q1;
    q1.setRotate(side, rAxis);
    Quat4f q2;
    q2.setRotate(rAxis, mRotateRate);
    pQuat->mult(q2, q1);
}

void CameraFrontAndBack::calcDipQuat(Quat4f* pDipQuat, const Vec3f& rFront, const Vec3f& rDir) {
    Vec3f side = rDir;
    MR::normalize(&side);
    f32 angle = side.angle(rFront);
    f32 rate = 1.0f;
    if (angle > mMaxDipAngle) {
        rate = mMaxDipAngle / angle;
    }
    pDipQuat->setRotate(rFront, side, rate);
}
