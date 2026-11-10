#include "Game/Gravity.hpp"
#include "Game/Util.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_DiskGravity_f1, "DiskGravity.f1", 2500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_DiskGravity_f2, "DiskGravity.f2", 2500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_DiskGravity_f3, "DiskGravity.f3", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLocalParam_f1, "updateLocalParam.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLocalParam_f2, "updateLocalParam.f2", 0.5f);

DiskGravity::DiskGravity()
    : PlanetGravity(), mLocalPosition(0.0f, 50.0f, 0.0f), mWorldPosition(0.0f, 50.0f, 0.0f), mLocalNormal(0, 1, 0), mWorldNormal(0, 1, 0),
      mSideDirection(1, 0, 0), mOppositeSideVecOrtho(1, 0, 0), mWorldOppositeSideVecOrtho(1, 0, 0) {
    mLocalRadius = cNum_DiskGravity_f1;
    mWorldRadius = cNum_DiskGravity_f2;
    mValidDegree = cNum_DiskGravity_f3;
    mValidCos = -1.0f;
    mEnableBothSide = true;
    mEnableEdgeGravity = true;
}

void DiskGravity::setLocalPosition(const Vec3f& rLocalPos) {
    mLocalPosition = rLocalPos;
}

void DiskGravity::setLocalDirection(const Vec3f& rLocalDir) {
    mLocalNormal.set(rLocalDir);
    MR::normalizeOrZero(&mLocalNormal);
    updateLocalParam();
}

void DiskGravity::setSideDirection(const Vec3f& rSideDir) {
    mSideDirection.set(rSideDir);
    updateLocalParam();
}

void DiskGravity::setRadius(f32 radius) {
    mLocalRadius = radius;
}

void DiskGravity::setValidDegree(f32 validDegree) {
    mValidDegree = validDegree;
    updateLocalParam();
}

void DiskGravity::setBothSide(bool isBothSide) {
    mEnableBothSide = isBothSide;
}

void DiskGravity::setEnableEdgeGravity(bool isEnable) {
    mEnableEdgeGravity = isEnable;
}

bool DiskGravity::calcOwnGravityVector(Vec3f* pDest, f32* pDistance, const Vec3f& rPosition) const {
    Vec3f relativePos;
    relativePos = rPosition - mWorldPosition;

    f32 centralAxisY = relativePos.dot(mWorldNormal);

    if (!mEnableBothSide && centralAxisY < 0.0f)
        return false;

    Vec3f dirOnDiskPlane = relativePos - mWorldNormal * centralAxisY;
    f32 distanceToCentralAxis;
    MR::separateScalarAndDirection(&distanceToCentralAxis, &dirOnDiskPlane, dirOnDiskPlane);

    if (mValidCos > -1.0f && dirOnDiskPlane.dot(mWorldOppositeSideVecOrtho) < mValidCos)
        return false;

    Vec3f gravity(0, 0, 0);
    f32 distance = 0.0f;

    if (distanceToCentralAxis <= mWorldRadius) {
        gravity = centralAxisY >= 0.0f ? -mWorldNormal : mWorldNormal;
        distance = MR::abs(centralAxisY);
    } else {
        if (!mEnableEdgeGravity) {
            return false;
        }

        Vec3f closestEdgePoint;
        closestEdgePoint.set(dirOnDiskPlane * mWorldRadius);
        closestEdgePoint += mWorldPosition;

        gravity = closestEdgePoint - rPosition;
        MR::separateScalarAndDirection(&distance, &gravity, gravity);
    }

    if (!isInRangeDistance(distance)) {
        return false;
    }

    // Set gravity and scalar results
    if (pDest) {
        *pDest = gravity;
    }
    if (pDistance) {
        *pDistance = distance;
    }

    return true;
}

void DiskGravity::updateMtx(const Pos3f& rMtx) {
    rMtx.mult(mLocalPosition, mWorldPosition);
    rMtx.mult33(mLocalNormal, mWorldNormal);
    rMtx.mult33(mOppositeSideVecOrtho, mWorldOppositeSideVecOrtho);

    f32 axisScale;
    MR::separateScalarAndDirection(&axisScale, &mWorldNormal, mWorldNormal);
    mWorldRadius = mLocalRadius * axisScale;
}

void DiskGravity::updateLocalParam() {
    Rot3f rot;

    mValidCos = MR::cosDegree(cNum_updateLocalParam_f1 * mValidDegree);
    if (MR::isNearZero(mLocalNormal)) {
        mOppositeSideVecOrtho.zero();
        return;
    }
    mOppositeSideVecOrtho.killElement2(mSideDirection, mLocalNormal);
    MR::normalizeOrZero(&mOppositeSideVecOrtho);
    if (MR::isNearZero(mOppositeSideVecOrtho)) {
        mOppositeSideVecOrtho.zero();
        return;
    }

    rot.identity();
    rot.setRotate(mLocalNormal, cNum_updateLocalParam_f2 * mValidDegree * (PI / 180));
    rot.mult(mOppositeSideVecOrtho, mOppositeSideVecOrtho);
}
