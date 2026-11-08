#include "Game/Gravity.hpp"
#include "Game/Util.hpp"

WireGravity::WireGravity() {
}

void WireGravity::setPointListSize(u32 numPoints) {
    mPoints.init(numPoints);
    mPoints.clear();
}

void WireGravity::addPoint(const Vec3f& rPoint) {
    mPoints.mArray.mArr[mPoints.mCount++] = rPoint;
}

bool WireGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPos) const {
    f32 distance = -1.0f;
    Vec3f pointOfAttraction;
    for (s32 i = 0; i < mPoints.size() - 1; i++) {
        Vec3f positionProjectedOntoWire;
        MR::calcPerpendicFootToLineInside(&positionProjectedOntoWire, rPos, mPoints[i], mPoints[i + 1]);

        f32 squareDistance = rPos.squared(positionProjectedOntoWire);
        if (squareDistance < distance || distance < 0.0f) {
            pointOfAttraction = positionProjectedOntoWire;
            distance = squareDistance;
        }
    }

    if (!isInRangeSquare(distance)) {
        return false;
    }

    if (distance >= 0.0f) {
        MR::separateScalarAndDirection(pScalar, pDest, pointOfAttraction - rPos);

        return true;
    }

    return false;
}
