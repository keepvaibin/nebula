#include "Game/Camera/CameraInwardTower.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f1, "calcIdealPose.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f2, "calcIdealPose.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f3, "calcIdealPose.f3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f4, "calcIdealPose.f4", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sRoundThreshold, 0.3f);
    NEBULA_TUNABLE(f32, sAttenuation, 20.0f);
    NEBULA_TUNABLE(f32, sResetRotSpeed, 0.2f);
};  // namespace

CameraInwardTower::CameraInwardTower(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 1.0f, 0.0f), mAngleX(), mAngleMax(), mDist(2000.0f), mPos(0.0f, 10.0f, 0.0f), mAngleY(),
      mIsResetting() {
    lumacore::setClass(this, lumacore::kClassId< CameraInwardTower >);
    createVPanObj();
}

void CameraInwardTower::reset() {
    mVPan->resetJump();
    mVPan->calcWatchPos(nullptr);
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getWatchUpVec(mCameraMan));
    mVPan->reset();
    mPos.set(CameraLocalUtil::getPos(this));
    mIsResetting = false;
    mAngleY = 0.0f;
}

CameraTargetObj* CameraInwardTower::calc() {
    mVPan->updateJump();
    mVPan->calcWatchPos(nullptr);
    calcIdealPose();
    mPos.set(CameraLocalUtil::getWatchPos(this));
    mVPan->arrange();
    return CameraLocalUtil::getTarget(this);
}

void CameraInwardTower::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, CameraLocalUtil::getNormalizedOr(general->mAxis, Vec3f(0.0f, 1.0f, 0.0f)), Vec2f(general->mAngleB, general->mAngleA), general->mDist);
}

void CameraInwardTower::calcIdealPose() {
    Vec3f watchPos = CameraLocalUtil::getWatchPos(this);
    Vec3f axis = mAxis;
    mZoneMatrix.mult33(axis, axis);

    Vec3f wPoint = mWPoint;
    mZoneMatrix.mult(wPoint, wPoint);

    Vec3f outDir = watchPos - wPoint;
    outDir.set(outDir - axis * axis.dot(outDir));
    if (MR::isNearZero(outDir)) {
        return;
    }
    MR::normalize(&outDir);

    Vec3f up = -CameraLocalUtil::getTarget(this)->getGravityVector();
    Vec3f camFront = watchPos - mPos;
    MR::normalizeOrZero(&camFront);

    Vec3f camUp = outDir.cross(camFront);
    f32 rate = camUp.length();
    if (camUp.dot(up) < 0.0f) {
        rate = -rate;
    }

    f32 speed, angle;
    angle = mAngleY;
    speed = mAngleMax * MR::abs(rate) / ::sAttenuation;
    if (CameraLocalUtil::tryCameraResetTrigger()) {
        mIsResetting = true;
    }

    if (mIsResetting) {
        if (mAngleY >= 0.0f) {
            mAngleY -= ::sResetRotSpeed;
            if (mAngleY < 0.0f) {
                mAngleY = 0.0f;
                mIsResetting = false;
            }
        } else {
            mAngleY += ::sResetRotSpeed;
            if (mAngleY > 0.0f) {
                mAngleY = 0.0f;
                mIsResetting = false;
            }
        }
    } else {
        if (rate > ::sRoundThreshold) {
            angle += speed;
            if (angle > mAngleMax) {
                angle = mAngleMax;
            }
            mAngleY = mAngleY * cNum_calcIdealPose_f1 + angle * cNum_calcIdealPose_f2;
        } else if (rate < -::sRoundThreshold) {
            angle -= speed;
            if (angle < -mAngleMax) {
                angle = -mAngleMax;
            }
            mAngleY = mAngleY * cNum_calcIdealPose_f3 + angle * cNum_calcIdealPose_f4;
        }
    }

    Pos3f rotY;
    rotY.makeRotate(up, mAngleY);

    Vec3f front = outDir;
    rotY.mult(front, front);
    Vec3f side = up;
    side.cross(side, front);
    if (MR::isNearZero(side)) {
        return;
    }
    MR::normalize(&side);

    Pos3f rotX;
    rotX.makeRotate(side, mAngleX);
    rotX.mult(front, front);

    Vec3f pos = watchPos - front * mDist;
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
}
