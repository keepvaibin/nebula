#include "Game/Camera/CameraInnerCylinder.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraInnerCylinder::CameraInnerCylinder(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 0.0f, 0.0f), mAngleA(), mAngleB(), mDist(), mLastWatchPoint(0.0f, 0.0f, 0.0f),
      mIsNotInitialized() {
    lumacore::setClass(this, lumacore::kClassId< CameraInnerCylinder >);
}

void CameraInnerCylinder::reset() {
    mIsNotInitialized = false;
}

CameraTargetObj* CameraInnerCylinder::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeGroundWatchPoint(&watchPoint, this, &mLastWatchPoint, &mIsNotInitialized);

    Pos3f zoneMtx;
    zoneMtx.set(mZoneMatrix);
    Pos3f invMtx = zoneMtx;
    invMtx.invert(invMtx);
    invMtx.mult(watchPoint, watchPoint);

    Vec3f groundPos = CameraLocalUtil::getTarget(this)->getGroundPos();
    invMtx.mult(groundPos, groundPos);

    Vec3f front = groundPos - mWPoint;
    front -= mAxis * front.dot(mAxis);
    if (MR::isNearZero(front)) {
        return nullptr;
    }

    MR::normalize(&front);

    Vec3f up(0.0f, 1.0f, 0.0f);
    Vec3f side = up.cross(mAxis);
    if (MR::isNearZero(side)) {
        return nullptr;
    }

    MR::normalize(&side);

    Vec3f camUp = mAxis.cross(side);
    f32 angleX = camUp.dot(front);
    f32 angleY = side.dot(front);

    Vec3f watchDir = watchPoint - groundPos;
    Vec3f watchSide = side * side.dot(watchDir);

    Vec3f watchFront = watchDir - watchSide;
    watchSide *= mSideDist;
    watchPoint.set(watchSide + watchFront + groundPos);

    Pos3f frame;
    frame.setXDir(side);
    frame.setYDir(camUp);
    frame.setZDir(mAxis);
    frame.setTrans(watchPoint);

    Pos3f pitchMtx;
    pitchMtx.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mAngleA * angleX);

    Pos3f yawMtx;
    yawMtx.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), -mAngleB * angleY);

    frame.concat(frame, yawMtx);
    frame.concat(frame, pitchMtx);

    Vec3f pos(0.0f, 0.0f, mDist);
    frame.mult(pos, pos);
    zoneMtx.mult(watchPoint, watchPoint);
    zoneMtx.mult33(up, up);
    zoneMtx.mult(pos, pos);

    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, up);

    return CameraLocalUtil::getTarget(this);
}

void CameraInnerCylinder::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(0.0f, 0.0f, 1.0f)), Vec2f(general->mAngleA, general->mAngleB), general->mDist, general->mUp.x);
}
