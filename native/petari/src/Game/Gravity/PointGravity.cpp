#include "Game/Gravity.hpp"
#include "Game/Util.hpp"

PointGravity::PointGravity() : PlanetGravity(), mOrigPosition(0, 0, 0), mTranslation(0, 0, 0) {
}

bool PointGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const {
    Vec3f direction = mTranslation - rPosition;
    const f32 distance = direction.length();

    if (!isInRangeDistance(distance)) {
        return false;
    }

    if (pDest != nullptr) {
        if (MR::isNearZero(distance)) {
            direction.zero();
        } else {
            const f32 scalar = 1.0f / distance;
            direction.x *= scalar;
            direction.y *= scalar;
            direction.z *= scalar;
        }
        *pDest = direction;
    }
    if (pScalar != nullptr) {
        *pScalar = distance;
    }

    return true;
}

void PointGravity::updateMtx(const Pos3f& rMtx) {
    // World position of the zone-local point.
    rMtx.mult(mOrigPosition, mTranslation);
}
