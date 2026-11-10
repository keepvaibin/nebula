#include "Game/Camera/CameraPointer.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_reset_f1, "reset.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f4, "calc.f4", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f5, "calc.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f6, "calc.f6", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f7, "calc.f7", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f8, "calc.f8", 38.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f9, "calc.f9", 1000.0f);

namespace {
    NEBULA_TUNABLE(f32, sMaxDistance, 0.15f);
    NEBULA_TUNABLE(f32, sFovyBlendRate, 0.05f);
};  // namespace

CameraPointer::CameraPointer(const char* pName)
    : Camera(pName), mPointerDistance(), mRealPointPos(0.0f, 0.0f), mBlendPointPos(0.0f, 0.0f), mResetPointingPos(true), mCameraType(), mDist(),
      mFovy(40.0f), mAngleXRange(PI / 2.0f), mAngleYRange(PI / 2.0f), mAngleX(), mSpeedBlendRate(0.05f), mBlendFriction(0.99f),
      mAngleYMin() {
    mMtx.identity();
}

void CameraPointer::reset() {
    if (mCameraType == CameraType_FixedPos) {
        CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
        Vec3f pos = (CameraLocalUtil::getTarget(this)->getPosition() + Vec3f(0.0f, cNum_reset_f1, 0.0f));
        Vec3f front = pos - CameraLocalUtil::getPos(this);
        MR::normalize(&front);
        Vec3f up = CameraLocalUtil::getTarget(this)->getUpVec();
        Vec3f side = up.cross(front);
        MR::normalize(&side);
        up.cross(front, side);
        mMtx.identity();
        mMtx.setXDir(side);
        mMtx.setYDir(up);
        mMtx.setZDir(front);
    } else if (mCameraType == CameraType_FollowWorldPos) {
        Vec3f worldPos;
        worldPos.set(*MR::getStarPointerWorldPosUsingDepth(PLAYER_1));
        Vec3f basePos = CameraLocalUtil::getTarget(this)->getPosition() + CameraLocalUtil::getTarget(this)->getUpVec() * mDist;
        Vec3f front = worldPos - basePos;
        if (MR::isNearZero(front)) {
            front.set(CameraLocalUtil::getTarget(this)->getFrontVec());
        } else {
            MR::normalize(&front);
        }

        Vec3f up = CameraLocalUtil::getTarget(this)->getUpVec();
        Vec3f side = up.cross(front);
        if (MR::isNearZero(side)) {
            CameraMan* camMan = mCameraMan;
            Vec3f diff = CameraLocalUtil::getWatchPos(camMan) - CameraLocalUtil::getPos(camMan);
            Quat4f rot;
            rot.setRotate(diff, front);
            side.cross(CameraLocalUtil::getUpVec(mCameraMan), diff);
            rot.transform(side);
        } else {
            MR::normalize(&side);
        }
        up.cross(front, side);
        mMtx.identity();
        mMtx.setXDir(side);
        mMtx.setYDir(up);
        mMtx.setZDir(front);
    }

    MR::getPointerPos(&mRealPointPos, PLAYER_1);
    mResetPointingPos = true;
    mBlendPointPos.set(0.0f, 0.0f);
    mRealPointPos.set(0.0f, 0.0f);
}

CameraTargetObj* CameraPointer::calc() {
    if (mCameraType == CameraType_UpdateWithTarget) {
        mMtx.identity();
        mMtx.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
        mMtx.setYDir(CameraLocalUtil::getTarget(this)->getUpVec());
        mMtx.setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());
        CameraLocalUtil::setPos(this, CameraLocalUtil::getTarget(this)->getPosition() + CameraLocalUtil::getTarget(this)->getUpVec() * mDist);
    } else if (mCameraType == CameraType_FollowWorldPos) {
        CameraLocalUtil::setPos(this, CameraLocalUtil::getTarget(this)->getPosition() + CameraLocalUtil::getTarget(this)->getUpVec() * mDist);
    }

    Vec2f pointPos;
    MR::getPointerPos(&pointPos, PLAYER_1);

    if (mResetPointingPos) {
        pointPos.set(0.0f, 0.0f);
        if (MR::abs(mRealPointPos.x) < cNum_calc_f1 && MR::abs(mRealPointPos.y) < cNum_calc_f2) {
            mResetPointingPos = false;
        }
    }

    if (MR::isPointerInScreen(PLAYER_1)) {
        mRealPointPos = mRealPointPos * cNum_calc_f3 + pointPos * cNum_calc_f4;
        if (mCameraType != CameraType_FollowWorldPos) {
            if (MR::isPressedCrouch(PLAYER_1)) {
                mPointerDistance = MR::getPointerDistance(PLAYER_1);
            }
            if (MR::isHeldCrouch(PLAYER_1)) {
                f32 updateDist = MR::clamp(mPointerDistance - MR::getPointerDistance(PLAYER_1), -::sMaxDistance, ::sMaxDistance);
                mFovy =
                    (cNum_calc_f5 - ((updateDist * cNum_calc_f6) / ::sMaxDistance)) * (1.0f - ::sFovyBlendRate) + CameraLocalUtil::getFovy(this) * ::sFovyBlendRate;
            }
        }
    }

    pointPos.x = MR::clamp(mRealPointPos.x, -1.0f, 1.0f);
    pointPos.y = MR::clamp(mRealPointPos.y, -1.0f, 1.0f);

    f32 moveSpeed = MR::clamp01((pointPos - mBlendPointPos).length() / MR::sqrt(cNum_calc_f7));

    f32 t = 1.0f - moveSpeed * mSpeedBlendRate;
    t *= mBlendFriction;
    mBlendPointPos = mBlendPointPos * t + pointPos * (1.0f - t);

    if (mCameraType != CameraType_FollowWorldPos) {
        if (!MR::isHeldCrouch(PLAYER_1)) {
            mFovy = CameraLocalUtil::getFovy(this) * ::sFovyBlendRate + cNum_calc_f8;
        }
        CameraLocalUtil::setFovy(this, mFovy);
    }

    Pos3f panYMtx;
    panYMtx.identity();
    panYMtx.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mBlendPointPos.y * mAngleYRange);

    Pos3f panXMtx;
    panXMtx.identity();
    panXMtx.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), -mBlendPointPos.x * mAngleXRange);

    Pos3f tiltMtx;
    tiltMtx.identity();
    tiltMtx.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), -mAngleX);

    Pos3f rotMtx;
    rotMtx.concat(panXMtx, panYMtx);
    rotMtx.concat(tiltMtx, rotMtx);
    rotMtx.concat(mMtx, rotMtx);
    Vec3f front;
    rotMtx.getZDir(front);
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getPos(this) + front * cNum_calc_f9);
    Vec3f up;
    rotMtx.getYDir(up);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);

    return nullptr;
}

void CameraPointer::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mNum1, general->mDist, general->mAngleA, general->mAngleB, general->mWPoint.z, general->mWPoint.x, general->mWPoint.y, general->mUp.x);
}
