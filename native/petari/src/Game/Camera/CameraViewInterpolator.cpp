#include "Game/Camera/CameraViewInterpolator.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/AreaObj/CameraRepulsiveArea.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcCollision_f1, "calcCollision.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCollision_f2, "calcCollision.f2", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCollision_f3, "calcCollision.f3", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBinder_f1, "calcBinder.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_reduceOscillation_f1, "reduceOscillation.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_reduceOscillation_f2, "reduceOscillation.f2", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sCollisionBallRadius, 10000.0f);
    NEBULA_TUNABLE(f32, sCollisionThroughDistance, 3400.0f);
    NEBULA_TUNABLE(f32, sNearZ, 100.0f);
};  // namespace

CameraViewInterpolator::CameraViewInterpolator()
    : mInterpolateTimer(), mInterpolateTime(), mIsAntiOscillation(true), mIsForceCameraChange(true), mIsInterpolationOff(),
      mTargetPosition(0.0f, 0.0f, 0.0f), mRate(0.7f), mIsCollisionOff(), mIsRepulsionOff(), mTargetFovy(45.0f), mFovy(45.0f),
      mIsInterpolatingNearlyEnd(true), mIsInterpolating(), mIsCorrectErpPositionOn(true), mCalcState(), mTargetObj(),
      mBinder(new Binder(nullptr, &mPosition, &mGravity, 100.0f, 0.0f, 64)), mPosition(0.0f, 0.0f, 0.0f), mGravity(0.0f, -1.0f, 0.0f),
      mIsCollided(true) {
    mTargetMtx.identity();
    mMtx.identity();
    mBinder->setTriangleFilter(MR::createTriangleFilterFunc(MR::isCameraCodeThrough));
}

void CameraViewInterpolator::updateCameraMtx(MtxPtr pMtx, const Vec3f& rPos, const CameraTargetObj* pCameraObj, f32 fovy) {
    Vec3f targetPos = mTargetPosition;
    Pos3f targetMtx = mTargetMtx;

    updateCalcState(pCameraObj);
    interpolateCameraSwitching(pMtx, rPos, fovy);

    if (!mIsRepulsionOff && translateByRepulsion()) {
        lookAtCenter();
    }

    calcCollision(targetMtx);
    mIsCollisionOff = false;
    reduceOscillation();

    Pos3f inv;
    inv.invert(mMtx);
    MR::setCameraViewMtx(inv);
    MR::setFovy(mFovy);

    mIsInterpolationOff = false;
    checkNearlyEnd(pMtx);
    if (!isInterpolating()) {
        mIsInterpolating = false;
    }
}

void CameraViewInterpolator::setInterpolation(u32 time) {
    if (!mIsInterpolationOff) {
        mInterpolateTimer = 0;
        mInterpolateTime = time;

        if (time == 0) {
            mIsForceCameraChange = true;
        }

        if (time == 0) {
            mIsInterpolatingNearlyEnd = true;
        } else {
            mIsInterpolatingNearlyEnd = false;
        }
    }

    mIsInterpolating = false;
}

bool CameraViewInterpolator::isInterpolating() const {
    return mInterpolateTime != 0 && mInterpolateTime > mInterpolateTimer;
}

bool CameraViewInterpolator::isInterpolatingNearlyEnd() const {
    if (isInterpolating()) {
        return mIsInterpolatingNearlyEnd;
    } else {
        return true;
    }
}

void CameraViewInterpolator::lookAtCenter() {
    Vec3f pos;
    mTargetMtx.getTrans(pos);
    Vec3f front;
    mTargetMtx.getZDir(front);

    Vec3f targetDir = pos - mTargetPosition;
    if (MR::isNearZero(targetDir)) {
        return;
    }

    MR::normalize(&targetDir);

    Pos3f rot;
    rot.setRotate(front, targetDir);

    mTargetMtx.setTrans(Vec3f(0.0f, 0.0f, 0.0f));
    mTargetMtx.concat(rot, mTargetMtx);
    mTargetMtx.setTrans(pos);
}

bool CameraViewInterpolator::calcCollision(const Pos3f& rMtx) {
    if (mIsCollisionOff || mIsInterpolating) {
        return false;
    }

    Vec3f pos;
    rMtx.getTrans(pos);
    Vec3f targetPos;
    mTargetMtx.getTrans(targetPos);

    mPosition.set(pos);
    resetGravity();

    // + 0 turns -0 into +0.
    targetPos += Vec3f(0.0f, 0.0f, 0.0f);

    Vec3f bindDir;
    if (!calcBinder(&bindDir, targetPos, pos)) {
        mIsCollided = true;
        return false;
    }

    mIsCollided = false;

    bindDir *= cNum_calcCollision_f1;
    Vec3f newPos = pos;
    newPos += bindDir;
    Vec3f front = newPos - mTargetPosition;
    if (MR::isNearZero(front)) {
        return false;
    }

    MR::normalize(&front);

    Vec3f prevUp;
    rMtx.getYDir(prevUp);
    Vec3f up;
    mTargetMtx.getYDir(up);

    if (MR::abs(up.dot(front)) < cNum_calcCollision_f2) {
        Vec3f side = up.cross(front);
        Vec3f newUp = front.cross(side);
        if (!MR::isNearZero(newUp)) {
            MR::normalize(&newUp);
            mTargetMtx.setXDir(side);
            mTargetMtx.setYDir(newUp);
            mTargetMtx.setZDir(front);
            mTargetMtx.setTrans(newPos);
        } else {
            mTargetMtx.setTrans(newPos);
        }
    } else if (MR::abs(prevUp.dot(front)) < cNum_calcCollision_f3) {
        Vec3f side = prevUp.cross(front);
        Vec3f newUp = front.cross(side);
        if (!MR::isNearZero(newUp)) {
            MR::normalize(&newUp);
            mTargetMtx.setXDir(side);
            mTargetMtx.setYDir(newUp);
            mTargetMtx.setZDir(front);
            mTargetMtx.setTrans(newPos);
        } else {
            mTargetMtx.setTrans(newPos);
        }
    } else {
        mTargetMtx.setTrans(newPos);
    }

    return true;
}

bool CameraViewInterpolator::calcBinder(Vec3f* pBindDir, const Vec3f& rTargetPos, const Vec3f& rPos) {
    Vec3f diff = rTargetPos - rPos;
    f32 dist = diff.length();
    if (dist >= ::sCollisionThroughDistance) {
        mBinder->clear();
        return false;
    }

    mPosition.set(rPos);
    Vec3f grav = Vec3f(0.0f, -1.0f, 0.0f);
    mGravity.set(grav);

    // The near plane the picture is drawn with: the render field of view (taller below 4:3) and the screen's own aspect,
    // so the ball keeps the near plane out of walls on any screen, not only 16:9.
    f32 nearPlaneHeight = MR::tanDegree(MR::getRenderFovy() * cNum_calcBinder_f1) * ::sNearZ;
    f32 nearPlaneWidth = nearPlaneHeight * MR::getAspect();

    mBinder->mRadius = MR::sqrt(::sCollisionBallRadius + nearPlaneHeight * nearPlaneHeight + nearPlaneWidth * nearPlaneWidth);
    *pBindDir = mBinder->bind(diff);

    if (mBinder->mPlaneNum == 0) {
        return false;
    }

    return true;
}

void CameraViewInterpolator::reduceOscillation() {
    if (mIsForceCameraChange) {
        MotionSignal::cameraCut();
    }
    if (!mIsAntiOscillation || mIsForceCameraChange) {
        mIsForceCameraChange = false;
        mMtx.set(mTargetMtx);
        mFovy = mTargetFovy;
        return;
    }

    Quat4f targetRot;
    mTargetMtx.getQuat(targetRot);
    Vec3f targetPos;
    mTargetMtx.getTrans(targetPos);

    Quat4f rot;
    mMtx.getQuat(rot);
    Vec3f pos;
    mMtx.getTrans(pos);

    f32 invRate = 1.0f - mRate;
    Quat4f interpRot;
    rot.slerp(targetRot, invRate);
    rot.normalize();
    pos = pos * mRate + targetPos * invRate;

    mMtx.makeQuat(rot);
    mMtx.setTrans(pos);
    mFovy = mFovy * cNum_reduceOscillation_f1 + mTargetFovy * cNum_reduceOscillation_f2;
}

void CameraViewInterpolator::updateCalcState(const CameraTargetObj* pTargetObj) {
    switch (mCalcState) {
    case CalcState_Ready: {
        if (pTargetObj == nullptr || mTargetObj != pTargetObj) {
            mCalcState = CalcState_Invalid;
            break;
        }

        mTargetPosition += pTargetObj->getLastMove();
        if (mIsCorrectErpPositionOn) {
            Vec3f targetPos;
            mTargetMtx.getTrans(targetPos);
            targetPos += pTargetObj->getLastMove();
            mTargetMtx.setTrans(targetPos);
        }

        break;
    }

    default: {
        if (pTargetObj != nullptr && (mInterpolateTime == 0 || mInterpolateTimer >= mInterpolateTime)) {
            mCalcState = CalcState_Ready;
        }

        break;
    }
    }

    mTargetObj = pTargetObj;
    mIsCorrectErpPositionOn = true;
}

void CameraViewInterpolator::interpolateCameraSwitching(MtxPtr pMtx, const Vec3f& rPos, f32 fovy) {
    if (mInterpolateTime == 0) {
        mTargetPosition.set(rPos);
        mTargetMtx.set(pMtx);
        mTargetFovy = fovy;
        return;
    }

    Pos3f newMtx = pMtx;

    Quat4f rot;
    mTargetMtx.getQuat(rot);
    Vec3f up;
    mTargetMtx.getZDir(up);
    Vec3f pos;
    mTargetMtx.getTrans(pos);

    Quat4f newTargetRot;
    newMtx.getQuat(newTargetRot);
    Vec3f newTargetUp;
    newMtx.getZDir(newTargetUp);
    Vec3f newTargetPos;
    newMtx.getTrans(newTargetPos);

    f32 rate = static_cast< f32 >(mInterpolateTimer) / static_cast< f32 >(mInterpolateTime);
    rate *= rate;

    CameraLocalUtil::slerpCamera(&rot, rot, newTargetRot, rate, false);
    rot.normalize();

    Pos3f mtx;
    mtx.makeQuat(rot);
    f32 currTargetDist = (mTargetPosition - pos).length();
    f32 newTargetDist = (rPos - newTargetPos).length();
    f32 dist = currTargetDist + rate * (newTargetDist - currTargetDist);

    mTargetPosition += (rPos - mTargetPosition) * rate;

    Vec3f newUp;
    mtx.getZDir(newUp);

    pos = mTargetPosition + newUp * dist;
    mtx.setTrans(pos);

    mTargetFovy += rate * (fovy - mTargetFovy);

    mInterpolateTimer++;
    if (mInterpolateTimer > mInterpolateTime) {
        mInterpolateTimer = mInterpolateTime;
    }

    mTargetMtx.set(mtx);
}

void CameraViewInterpolator::checkNearlyEnd(MtxPtr pMtx) {
    Pos3f newMtx = pMtx;
    Vec3f pos;
    mTargetMtx.getTrans(pos);
    Vec3f newTargetPos;
    newMtx.getTrans(newTargetPos);

    if (pos.distance(newTargetPos) > 1.0f) {
        mIsInterpolatingNearlyEnd = false;
        return;
    }

    Pos3f mtxDiff;
    mtxDiff.set(mTargetMtx);
    mtxDiff.invert(mtxDiff);
    mtxDiff.concat(newMtx, mtxDiff);

    Vec3f rotAxis;
    if (mtxDiff.getRotate(rotAxis) > 1.0f) {
        mIsInterpolatingNearlyEnd = false;
        return;
    }

    mIsInterpolatingNearlyEnd = true;
}

bool CameraViewInterpolator::translateByRepulsion() {
    Vec3f pos;
    mTargetMtx.getTrans(pos);

    CameraRepulsiveArea* pArea = reinterpret_cast< CameraRepulsiveArea* >(MR::getAreaObj("CameraRepulsiveArea", pos));
    if (pArea == nullptr) {
        return false;
    }

    Vec3f repulsion = pArea->getRepulsion(pos);
    mTargetMtx.setTrans(pos + repulsion);
    return true;
}
