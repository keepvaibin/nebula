#include "Game/Gravity.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setBaseDistance_f1, "setBaseDistance.f1", 2000.0f);

ParallelGravity::ParallelGravity()
    : PlanetGravity(), mExtentX(0.0f), mExtentY(0.0f), mExtentZ(0.0f), mPlanePosition(0, 0, 0), mPlaneUpVec(0.0f, 1.0f, 0.0f),
      mWorldPlanePosition(0, 0, 0), mWorldPlaneUpVec(0.0f, 1.0f, 0.0f), mCylinderHeight(1000.0f), mCylinderRadius(500.0f), mBaseDistance(2000.0f),
      mRangeType(RangeType_Sphere), mDistanceCalcType(DistanceCalcType_Default) {
    mLocalMtx.identity();
    mWorldMtx.identity();
}

bool ParallelGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const {
    if (!isInRange(rPosition, pScalar)) {
        return false;
    }

    if (pDest != nullptr) {
        *pDest = -mWorldPlaneUpVec;
    }

    return true;
}

void ParallelGravity::updateMtx(const Pos3f& rMtx) {
    rMtx.mult33(mPlaneUpVec, mWorldPlaneUpVec);
    rMtx.mult(mPlanePosition, mWorldPlanePosition);
    MR::normalizeOrZero(&mWorldPlaneUpVec);

    if (mRangeType == RangeType_Box) {
        mWorldMtx.concat(rMtx, mLocalMtx);

        Vec3f tempDir;
        mWorldMtx.getXDir(tempDir);
        mExtentX = tempDir.squared();
        mWorldMtx.getYDir(tempDir);
        mExtentY = tempDir.squared();
        mWorldMtx.getZDir(tempDir);
        mExtentZ = tempDir.squared();
    }
}

void ParallelGravity::setPlane(const Vec3f& rPlaneUp, const Vec3f& rPlanePos) {
    mPlaneUpVec.normalize(rPlaneUp);
    mPlanePosition = rPlanePos;
}

void ParallelGravity::setRangeBox(const Pos3f& rMtx) {
    mLocalMtx = rMtx;
    updateIdentityMtx();
}

void ParallelGravity::setRangeCylinder(f32 radius, f32 height) {
    mCylinderRadius = radius;
    mCylinderHeight = height;
}

void ParallelGravity::setRangeType(RANGE_TYPE rangeType) {
    mRangeType = rangeType;
}

void ParallelGravity::setBaseDistance(f32 val) {
    if (val < 0.0f) {
        mBaseDistance = cNum_setBaseDistance_f1;
    } else {
        mBaseDistance = val;
    }
}

void ParallelGravity::setDistanceCalcType(DISTANCE_CALC_TYPE distanceCalcType) {
    mDistanceCalcType = distanceCalcType;
}

bool ParallelGravity::isInSphereRange(const Vec3f& rPosition, f32* pScalar) const {
    if (pScalar != nullptr) {
        *pScalar = mBaseDistance;
    }

    if (mRange < 0.0f) {
        return true;
    } else {
        Vec3f dirToCenter(mWorldPlanePosition - rPosition);
        f32 range = mRange;
        return dirToCenter.squared() < range * range;
    }
}

bool ParallelGravity::isInBoxRange(const Vec3f& rPosition, f32* pScalar) const {
    Vec3f translation;
    mWorldMtx.getTrans(translation);
    Vec3f dirToCenter(rPosition - translation);

    Vec3f dirX;
    mWorldMtx.getXDir(dirX);
    f32 dotX = dirToCenter.dot(dirX);

    if (dotX < -mExtentX || mExtentX < dotX) {
        return false;
    }

    Vec3f dirY;
    mWorldMtx.getYDir(dirY);
    f32 dotY = dirToCenter.dot(dirY);

    if (dotY < -mExtentY || mExtentY < dotY) {
        return false;
    }

    Vec3f dirZ;
    mWorldMtx.getZDir(dirZ);
    f32 dotZ = dirToCenter.dot(dirZ);

    if (dotZ < -mExtentZ || mExtentZ < dotZ) {
        return false;
    }

    if (pScalar != nullptr) {
        switch (mDistanceCalcType) {
        case DistanceCalcType_X:
            *pScalar = mBaseDistance + MR::abs(dotX) / MR::sqrt(mExtentX);
            break;
        case DistanceCalcType_Y:
            *pScalar = mBaseDistance + MR::abs(dotY) / MR::sqrt(mExtentY);
            break;
        case DistanceCalcType_Z:
            *pScalar = mBaseDistance + MR::abs(dotZ) / MR::sqrt(mExtentZ);
            break;
        case DistanceCalcType_Default:
            *pScalar = mBaseDistance;
            break;
        }
    }

    return true;
}

bool ParallelGravity::isInCylinderRange(const Vec3f& rPosition, f32* pScalar) const {
    f32 height = mWorldPlaneUpVec.dot(rPosition - mWorldPlanePosition);

    if (height < 0.0f || mCylinderHeight < height) {
        return false;
    }

    Vec3f positionOnWorldPlane;

    positionOnWorldPlane.killElement2(rPosition - mWorldPlanePosition, mWorldPlaneUpVec);

    f32 radius = positionOnWorldPlane.length();

    if (radius > mCylinderRadius) {
        return false;
    }

    *pScalar = mBaseDistance + radius;

    return true;
}

bool ParallelGravity::isInRange(const Vec3f& rPosition, f32* pScalar) const {
    switch (mRangeType) {
    case RangeType_Sphere:
        return isInSphereRange(rPosition, pScalar);
    case RangeType_Box:
        return isInBoxRange(rPosition, pScalar);
    case RangeType_Cylinder:
        return isInCylinderRange(rPosition, pScalar);
    }

    return false;
}
