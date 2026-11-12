#include "Game/Gravity.hpp"
#include "Game/Util.hpp"

DiskTorusGravity::DiskTorusGravity()
    : PlanetGravity(), mLocalPosition(0.0f, 50.0f, 0.0f), mTranslation(0.0f, 50.0f, 0.0f), mLocalDirection(0, 1, 0), mRotation(0, 1, 0) {
    mRadius = 2000.0f;
    mWorldRadius = 2000.0f;
    mDiskRadius = 0.0f;
    mEdgeType = 3;
    mEnableBothSide = true;
}

void DiskTorusGravity::setPosition(const Vec3f& rPosition) {
    mLocalPosition = rPosition;
}

void DiskTorusGravity::setDirection(const Vec3f& rDirection) {
    mLocalDirection = rDirection;
}

void DiskTorusGravity::setRadius(f32 radius) {
    mRadius = radius;
}

void DiskTorusGravity::setDiskRadius(f32 diskRadius) {
    if (diskRadius < 0.0f) {
        mDiskRadius = 0.0f;
    } else {
        mDiskRadius = diskRadius;
    }
}

void DiskTorusGravity::setEdgeType(s32 edgeType) {
    mEdgeType = edgeType;
}

void DiskTorusGravity::setBothSide(bool isBothSide) {
    mEnableBothSide = isBothSide;
}

void DiskTorusGravity::updateMtx(const Pos3f& rMtx) {
    rMtx.mult(mLocalPosition, mTranslation);
    rMtx.mult33(mLocalDirection, mRotation);

    f32 worldScale;
    MR::separateScalarAndDirection(&worldScale, &mRotation, mRotation);
    mWorldRadius = mRadius * worldScale;
}

bool DiskTorusGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPos) const {
    Vec3f relativePosition;
    relativePosition = rPos - mTranslation;

    f32 centralAxisY = relativePosition.dot(mRotation);

    if (!mEnableBothSide && centralAxisY < 0.0f) {
        return false;
    }

    Vec3f dirOnTorusPlane = relativePosition - mRotation * centralAxisY;
    f32 distanceToCentralAxis;
    MR::separateScalarAndDirection(&distanceToCentralAxis, &dirOnTorusPlane, dirOnTorusPlane);
    if (MR::isNearZero(distanceToCentralAxis)) {
        // Just choose a direction in-plane for gravity if the object is sitting
        // on the central axis (otherwise dirOnTorusPlane is zero)
        MR::makeAxisVerticalZX(&dirOnTorusPlane, mRotation);
    }

    f32 innerRadius = mWorldRadius - mDiskRadius;
    f32 worldRadius = mWorldRadius;
    Vec3f gravity(0, 0, 0);
    f32 distance = 0.0f;

    if (distanceToCentralAxis < innerRadius) {
        if (mEdgeType == EdgeType_DisableBoth || mEdgeType == EdgeType_DisableInner) {
            return false;
        }

        Vec3f nearestInnerEdgePoint;
        nearestInnerEdgePoint.scaleAdd(innerRadius, dirOnTorusPlane, mTranslation);

        gravity = nearestInnerEdgePoint - rPos;
        MR::separateScalarAndDirection(&distance, &gravity, gravity);
    } else if (distanceToCentralAxis > worldRadius) {
        if (mEdgeType == EdgeType_DisableBoth || mEdgeType == EdgeType_DisableOuter) {
            return false;
        }

        Vec3f nearestOuterEdgePoint;
        nearestOuterEdgePoint.scaleAdd(worldRadius, dirOnTorusPlane, mTranslation);
        gravity = nearestOuterEdgePoint - rPos;
        MR::separateScalarAndDirection(&distance, &gravity, gravity);
    } else {
        gravity = centralAxisY >= 0.0f ? -mRotation : mRotation;
        distance = MR::abs(centralAxisY);
    }

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
