#include "Game/Camera/CameraFrontAndBack.hpp"
#include "Game/Camera/CamTranslatorFrontAndBack.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    static const s32 sInterpolation = 120;
    static const f32 sWarpDistance = 500.0f;
};  // namespace

CameraFrontAndBack::~CameraFrontAndBack() {
}

CameraFrontAndBack::CameraFrontAndBack(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 1.0f, 0.0f), mRotateRate(), mMaxDipAngle(), mDist(1200.0f) {
}

void CameraFrontAndBack::reset() {
}

CameraTargetObj* CameraFrontAndBack::calc() {
    Vec3f pos = CameraLocalUtil::getPos(this);
    Vec3f axis = mAxis;
    mZoneMatrix.mult33(axis);
    Vec3f wPoint = mWPoint;
    mZoneMatrix.mult(wPoint, wPoint);

    Vec3f posDir = CameraLocalUtil::getTarget(this)->getPosition() - wPoint;
    posDir.orthogonalize(axis);
    if (MR::isNearZero(posDir)) {
        return nullptr;
    }

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);
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
    CameraLocalUtil::setPos(this, CameraLocalUtil::getTarget(this)->getPosition() + front * mDist);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
    if (pos.distance(CameraLocalUtil::getPos(this)) > ::sWarpDistance) {
        MR::startCameraInterpolation(::sInterpolation);
    }

    return CameraLocalUtil::getTarget(this);
}

CamTranslatorBase* CameraFrontAndBack::createTranslator() {
    return new CamTranslatorFrontAndBack(this);
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
