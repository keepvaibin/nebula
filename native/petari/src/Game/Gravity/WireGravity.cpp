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

#if defined(NEBULA_PLAIN_MODULE)
// The box round the wire's points and the range past it. isInRangeSquare squares the range, so its size counts.
bool WireGravity::calcBound(Vec3f* pCenter, f32* pRadius) const {
    if (mRange < 0.0f || mPoints.size() < 2) {
        return false;
    }

    Vec3f low(mPoints[0]);
    Vec3f high(mPoints[0]);
    for (s32 i = 1; i < mPoints.size(); i++) {
        const Vec3f& rPoint = mPoints[i];
        low.set(rPoint.x < low.x ? rPoint.x : low.x, rPoint.y < low.y ? rPoint.y : low.y, rPoint.z < low.z ? rPoint.z : low.z);
        high.set(rPoint.x > high.x ? rPoint.x : high.x, rPoint.y > high.y ? rPoint.y : high.y, rPoint.z > high.z ? rPoint.z : high.z);
    }

    *pCenter = (low + high) * 0.5f;
    *pRadius = 0.5f * (high - low).length() + MR::abs(mRange + mDistant);
    return true;
}
#endif
