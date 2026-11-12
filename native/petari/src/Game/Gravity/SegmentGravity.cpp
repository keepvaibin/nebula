#include "Game/Gravity.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>

SegmentGravity::SegmentGravity()
    : PlanetGravity(), mSideVector(1, 0, 0), mOppositeSideVecOrtho(1, 0, 0), mWorldOppositeSideVecOrtho(1, 0, 0), mAxis(0, 0, 0) {
    mAxisLength = 0.0f;
    mValidSideDegree = 360.0f;
    mValidSideCos = -1.0f;

    for (u32 i = 0; i < 2; i++) {
        mGravityPoints[i].zero();
        mWorldGravityPoints[i].zero();
        mHemisphereExtension[i] = 0.0f;
        mEdges[i] = true;
    }
}

bool SegmentGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPos) const {
    Vec3f relPosFromBase = rPos - mWorldGravityPoints[0];
    f32 axisY = relPosFromBase.dot(mAxis);
    if (-1.0f < mValidSideCos) {
        if (!mWorldOppositeSideVecOrtho.isZero()) {
            Vec3f dirOnBasePlane = relPosFromBase - mAxis * axisY;
            MR::normalizeOrZero(&dirOnBasePlane);
            if (dirOnBasePlane.dot(mWorldOppositeSideVecOrtho) < mValidSideCos) {
                return false;
            }
        }
    }
    Vec3f attraction;
    if (axisY < mHemisphereExtension[0]) {
        if (!mEdges[0]) {
            return false;
        } else {
            attraction = mWorldGravityPoints[0];
        }
    } else {
        if (axisY > mAxisLength - mHemisphereExtension[1]) {
            if (!mEdges[1]) {
                return false;
            }
            attraction = mWorldGravityPoints[1];
        } else {
            attraction = mWorldGravityPoints[0] + mAxis * axisY;
        }
    }
    Vec3f gravity = attraction - rPos;
    f32 distance;
    MR::separateScalarAndDirection(&distance, &gravity, gravity);
    if (!isInRangeDistance(distance)) {
        return false;
    }
    if (pDest) {
        *pDest = gravity;
    }
    if (pScalar) {
        *pScalar = distance;
    }
    return true;
}

void SegmentGravity::updateMtx(const Pos3f& rMtx) {
    for (s32 i = 0; i < 2; i++) {
        rMtx.mult(mGravityPoints[i], mWorldGravityPoints[i]);
    }
    rMtx.mult33(mOppositeSideVecOrtho, mWorldOppositeSideVecOrtho);
    mAxis = mWorldGravityPoints[1] - mWorldGravityPoints[0];
    MR::separateScalarAndDirection(&mAxisLength, &mAxis, mAxis);
}

void SegmentGravity::setGravityPoint(u32 index, const Vec3f& rGravityPoint) {
    mGravityPoints[index != 0] = rGravityPoint;
    updateLocalParam();
}

void SegmentGravity::setSideVector(const Vec3f& rSideVec) {
    mSideVector = rSideVec;
    MR::normalizeOrZero(&mSideVector);
    updateLocalParam();
}

void SegmentGravity::setValidSideDegree(f32 validSideDegree) {
    mValidSideDegree = validSideDegree;
    updateLocalParam();
}

void SegmentGravity::setEdgeValid(u32 index, bool isValid) {
    mEdges[index != 0] = isValid;
}

void SegmentGravity::updateLocalParam() {
    Rot3f rot;

    mValidSideCos = MR::cosDegree(0.5f * mValidSideDegree);

    Vec3f localAxis = mGravityPoints[1] - mGravityPoints[0];
    MR::normalizeOrZero(&localAxis);
    if (MR::isNearZero(localAxis)) {
        mOppositeSideVecOrtho.zero();
        return;
    }

    mOppositeSideVecOrtho.killElement2(mSideVector, localAxis);
    MR::normalizeOrZero(&mOppositeSideVecOrtho);
    if (MR::isNearZero(mOppositeSideVecOrtho)) {
        mOppositeSideVecOrtho.zero();
        return;
    }

    rot.identity();
    rot.setRotate(localAxis, 0.5f * mValidSideDegree * (PI / 180));
    rot.mult(mOppositeSideVecOrtho, mOppositeSideVecOrtho);
}
