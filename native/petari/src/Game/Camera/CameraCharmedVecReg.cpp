#include "Game/Camera/CameraCharmedVecReg.hpp"
#include "Game/Camera/CamTranslatorCharmedVecReg.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/VectorUtil.hpp"

namespace {
    static const f32 sRate = 0.02f;
};  // namespace

CameraCharmedVecReg::CameraCharmedVecReg(const char* pName)
    : Camera(pName), mName(), mDist(1000.0f), mAngleX(), mAngleYMin(), mAngleYMax(), mCamDistRatio(0.5f), mCamAngleRate(::sRate) {
}

void CameraCharmedVecReg::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}

CameraTargetObj* CameraCharmedVecReg::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    Vec3f watchOffs = watchPoint - CameraLocalUtil::getTarget(this)->getPosition();

    const Vec3f& vecReg = CameraLocalUtil::getVecReg(mName);

    Vec3f basePos = CameraLocalUtil::getTarget(this)->getPosition() * (1.0f - mCamDistRatio) + vecReg * mCamDistRatio;

    Vec3f up = -CameraLocalUtil::getTarget(this)->getGravityVector();
    Vec3f front = CameraLocalUtil::getTarget(this)->getPosition() - vecReg;
    MR::projectVecOnPlane(&front, front, up);

    if (MR::isNearZero(front)) {
        return nullptr;
    }
    MR::normalize(&front);

    Vec3f side = up.cross(front);
    if (MR::isNearZero(side)) {
        return nullptr;
    }
    MR::normalize(&side);

    Pos3f mtx;
    mtx.identity();
    mtx.setXYZDir(side, up, front);

    Vec3f camDir = CameraLocalUtil::getPos(this) - CameraLocalUtil::getTarget(this)->getPosition();
    MR::projectVecOnPlane(&camDir, camDir, up);
    if (MR::isNearZero(camDir)) {
        return nullptr;
    }
    MR::normalize(&camDir);

    Vec3f camUp = front.cross(camDir);
    f32 angleDiff = camDir.angle(front);
    f32 angle = angleDiff;
    if (angle < mAngleYMin) {
        angle = mAngleYMin;
    } else if (angle > mAngleYMax) {
        angle = mAngleYMax;
    }

    if (camUp.dot(up) < 0.0f) {
        angle = -angle;
        angleDiff = -angleDiff;
    }
    angle = angleDiff * (1.0f - mCamAngleRate) + angle * mCamAngleRate;

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), -mAngleX);

    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), angle);

    mtx.concat(rotY);
    mtx.concat(rotX);

    Vec3f posOffs(0.0f, 0.0f, mDist);
    mtx.mult33(posOffs);

    CameraLocalUtil::setWatchPos(this, basePos + watchOffs);
    CameraLocalUtil::setPos(this, posOffs + CameraLocalUtil::getTarget(this)->getPosition());
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, up);

    return CameraLocalUtil::getTarget(this);
}

CamTranslatorBase* CameraCharmedVecReg::createTranslator() {
    return new CamTranslatorCharmedVecReg(this);
}
