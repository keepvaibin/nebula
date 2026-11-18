#include "Game/Gravity.hpp"
#include "Game/Util.hpp"

PlanetGravity::PlanetGravity() {
    mRange = -1.0f;
    mDistant = 0.0f;
    mPriority = 0;
    mHost = this;
    mGravityType = GravityType_Normal;
    mGravityPower = GravityPower_Normal;
    mActivated = true;
    mIsInverse = false;
    mValidFollower = true;
    mIsRegistered = false;
    mAppeared = true;
#if defined(NEBULA_PLAIN_MODULE)
    mBoundCenter.zero();
    mBoundRadiusSquared = -1.0f;
    mBoundRange = mRange;
    mBoundDistant = mDistant;
#endif
}

void PlanetGravity::setPriority(s32 priority) {
    mPriority = priority;
}

bool PlanetGravity::calcGravity(Vec3f* pDest, const Vec3f& rPosition) const {
    // Calculate raw gravity vector
    f32 radius = 0.0f;
    Vec3f gravity;
    gravity.x = 0.0f;
    gravity.y = 0.0f;
    gravity.z = 0.0f;

    if (!calcOwnGravityVector(&gravity, &radius, rPosition))
        return false;

    // Adjust radius
    radius -= mDistant;

    if (radius < 1.0f) {
        radius = 1.0f;
    }

    // Apply gravity speed
    f32 scalar = 4000000.0f / (radius * radius);
    gravity.x *= scalar;
    gravity.y *= scalar;
    gravity.z *= scalar;

    // Invert vector if necessary
    if (mIsInverse) {
        gravity = -gravity;
    }

    // Set result vector
    pDest->x = gravity.x;
    pDest->y = gravity.y;
    pDest->z = gravity.z;

    return true;
}

bool PlanetGravity::isInRangeSquare(f32 radius) const {
    f32 range = mRange;

    if (range < 0.0f) {
        return true;
    } else {
        f32 distance = range + mDistant;
        return radius < distance * distance;
    }
}

bool PlanetGravity::isInRangeDistance(f32 radius) const {
    f32 range = mRange;

    if (range < 0.0f) {
        return true;
    } else {
        f32 distance = range + mDistant;
        return radius < distance;
    }
}

bool PlanetGravity::calcGravityFromMassPosition(Vec3f* pDirection, f32* pScalar, const Vec3f& rPosition, const Vec3f& rMassPosition) const {
    Vec3f direction = rMassPosition - rPosition;
    f32 scalar;

    MR::separateScalarAndDirection(&scalar, &direction, direction);

    if (!isInRangeDistance(scalar))
        return false;

    if (pDirection) {
        *pDirection = direction;
    }
    if (pScalar) {
        *pScalar = scalar;
    }

    return true;
}

void PlanetGravity::updateMtx(const Pos3f& /*rMtx*/) {
}

void PlanetGravity::updateIdentityMtx() {
    Pos3f mtx;
    mtx.identity();
    updateMtx(mtx);
#if defined(NEBULA_PLAIN_MODULE)
    refreshBound();
#endif
}

#if defined(NEBULA_PLAIN_MODULE)
void PlanetGravity::refreshBound() {
    mBoundRange = mRange;
    mBoundDistant = mDistant;

    Vec3f center;
    f32 radius;
    if (!calcBound(&center, &radius) || !(radius >= 0.0f)) {
        mBoundRadiusSquared = -1.0f;
        return;
    }

    // Room over the float rounding of the shapes' own tests.
    const f32 room = radius * 1.001f + 1.0f;
    mBoundCenter = center;
    mBoundRadiusSquared = room * room;
}
#endif
