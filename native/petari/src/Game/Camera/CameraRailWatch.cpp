#include "Game/Camera/CameraRailWatch.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraRailHolder.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);

CameraRailWatch::CameraRailWatch(const char* pName)
    : Camera(pName), mRailRider(), mDirection(), mSetDirection(), mRailCoordOffset(), mDist(1200.0f), mAngleX() {
    lumacore::setClass(this, lumacore::kClassId< CameraRailWatch >);
}

void CameraRailWatch::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));

    if (mRailRider == nullptr) {
        return;
    }

    if (mSetDirection == 0) {
        if (mRailRider->calcNearestPos(CameraLocalUtil::getPos(this)) <=
            mRailRider->calcNearestPos(CameraLocalUtil::getTarget(this)->getPosition())) {
            mDirection = 1;
        } else {
            mDirection = -1;
        }
    }
}

CameraTargetObj* CameraRailWatch::calc() {
    if (mRailRider == nullptr) {
        return nullptr;
    }

    CameraTargetObj* target = CameraLocalUtil::getTarget(this);
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, target, cNum_calc_f1 / cNum_calc_f2);

    if (mSetDirection != 0) {
        mDirection = mSetDirection;
    }

    f32 watchCoord = mRailRider->calcNearestPos(watchPoint);
    mRailRider->setCoord(watchCoord);

    Vec3f up = -target->getGravityVector();
    Vec3f front = mRailRider->mCurDirection * mDirection;
    Vec3f side = up.cross(front);
    up.cross(front, side);

    Pos3f mtx;
    mtx.setXDir(side);
    mtx.setYDir(up);
    mtx.setZDir(front);
    mtx.setTrans(Vec3f(0.0f, 0.0f, 0.0f));

    mRailRider->setCoord(watchCoord + mDirection * mRailCoordOffset);

    Vec3f watchFront = mRailRider->mCurPos - watchPoint;
    if (MR::isNearZero(watchFront)) {
        return nullptr;
    }
    MR::normalize(&watchFront);

    Pos3f rotMtx;
    rotMtx.setRotate(front, watchFront);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mAngleX);

    mtx.concat(mtx, rotX);
    mtx.concat(rotMtx, mtx);

    mtx.getYDir(up);
    mtx.getZDir(front);

    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setPos(this, watchPoint - front * mDist);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);

    return target;
}

void CameraRailWatch::setParam(s32 zoneID, s32 railID, s32 direction, f32 offset, f32 dist, f32 angleX) {
    mRailRider = MR::getCameraDirector()->mRailHolder->getRider(zoneID, railID);
    mSetDirection = direction;
    mRailCoordOffset = offset;
    mDist = dist;
    mAngleX = angleX;
}

void CameraRailWatch::setParamFromChunk(const CameraParamChunk* pChunk) {
    setParam(pChunk->getZoneID(), pChunk->mGeneralParam->mNum1, pChunk->mGeneralParam->mNum2, pChunk->mGeneralParam->mAxis.x, pChunk->mGeneralParam->mDist, pChunk->mGeneralParam->mAngleA);
}
