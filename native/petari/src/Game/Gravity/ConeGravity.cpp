#include "Game/Gravity.hpp"
#include "Game/Util/MathUtil.hpp"

ConeGravity::ConeGravity() : PlanetGravity() {
    mValidDegree = 360.0f;
    mValidCos = -1.0f;
    mWorldRadius = 0.0f;
    mEnableBottom = true;
    mTopCutRate = 0.0f;
    mLocalMtx.identity();
    mWorldMtx.identity();
}

void ConeGravity::setLocalMatrix(const Pos3f& rMtx) {
    mLocalMtx.setInline(rMtx);
    updateIdentityMtx();
}

void ConeGravity::setEnableBottom(bool isEnable) {
    mEnableBottom = isEnable;
}

void ConeGravity::setTopCutRate(f32 topCutRate) {
    mTopCutRate = topCutRate < 0.0f ? 0.0f : topCutRate > 1.0f ? 1.0f : topCutRate;
}

inline f32 absfInline(f32& orig, f32 v) {
    orig = v;
    return MR::abs(v);
}

void ConeGravity::updateMtx(const Pos3f& rMtx) {
    mWorldMtx.concat(rMtx, mLocalMtx);

    Vec3f sideVec;
    mWorldMtx.getXDir(sideVec);
    mWorldRadius = sideVec.length();
}

bool ConeGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPos) const {
    Vec3f worldBaseCenter, worldCentralAxis;

    mWorldMtx.getYDir(worldCentralAxis);
    mWorldMtx.getTrans(worldBaseCenter);

    Vec3f unitWorldCentralAxis;
    f32 centralAxisLength;
    MR::separateScalarAndDirection(&centralAxisLength, &unitWorldCentralAxis, worldCentralAxis);

    Vec3f relativePosition = rPos - worldBaseCenter;
    Vec3f positionOnBasePlane;
    positionOnBasePlane.killElement(relativePosition, unitWorldCentralAxis);

    if (MR::isNearZero(positionOnBasePlane)) {
        f32 positionOnCentralAxis;
        f32 distance = absfInline(positionOnCentralAxis, relativePosition.dot(unitWorldCentralAxis));

        if (positionOnCentralAxis > 0.0f) {
            f32 height = centralAxisLength * (1.0f - mTopCutRate);

            distance -= height;
            if (distance < 0.0f) {
                distance = 0.0f;
            }
        }

        if (!isInRangeDistance(distance)) {
            return false;
        }

        if (positionOnCentralAxis > 0.0f) {
            pDest->set(-unitWorldCentralAxis);
        } else {
            pDest->set(unitWorldCentralAxis);
        }

        *pScalar = distance;
        return true;
    }

    f32 distanceToCentralAxis = positionOnBasePlane.length();
    f32 centralAxisY = unitWorldCentralAxis.dot(relativePosition);

    bool isInsideCone = false;

    if (MR::isNearZero(centralAxisLength) || MR::isNearZero(mWorldRadius)) {
        // The cone is too small to have an inner region
        isInsideCone = false;
    } else if (distanceToCentralAxis < mWorldRadius - centralAxisY * (mWorldRadius / centralAxisLength)) {
        isInsideCone = true;
    }

    Vec3f pointOfAttraction;

    Vec3f dirOnDirectrix = worldBaseCenter + positionOnBasePlane * (mWorldRadius / distanceToCentralAxis);

    Vec3f apex = worldBaseCenter + worldCentralAxis;

    if (relativePosition.dot(worldCentralAxis) < 0.0f) {
        if (!mEnableBottom) {
            return false;
        }

        MR::calcPerpendicFootToLineInside(&pointOfAttraction, rPos, worldBaseCenter, dirOnDirectrix);

        if (MR::isNearZero(pointOfAttraction - rPos)) {
            *pDest = -unitWorldCentralAxis;
            *pScalar = 0.0f;

            return true;
        }

        else {
            return calcGravityFromMassPosition(pDest, pScalar, rPos, pointOfAttraction);
        }
    }

    if (mTopCutRate < 0.01f) {
        MR::calcPerpendicFootToLineInside(&pointOfAttraction, rPos, dirOnDirectrix, apex);
    } else {
        Vec3f generatrixTermination;
        generatrixTermination.set(apex * (1.0f - mTopCutRate) + dirOnDirectrix * mTopCutRate);

        Vec3f frustumBaseCenter;
        frustumBaseCenter.set(worldBaseCenter + worldCentralAxis * (1.0f - mTopCutRate));

        if ((rPos - generatrixTermination).dot(generatrixTermination - frustumBaseCenter) <= 0.0f) {
            // Attracted to the frustum
            f32 distanceToCentralAxis = unitWorldCentralAxis.dot(rPos - frustumBaseCenter);

            if (distanceToCentralAxis < 0.0f) {
                distanceToCentralAxis = 0.0f;
            }

            if (isInRangeDistance(distanceToCentralAxis)) {
                *pDest = -unitWorldCentralAxis;
                *pScalar = distanceToCentralAxis;
                return true;
            } else {
                return false;
            }
        }

        MR::calcPerpendicFootToLineInside(&pointOfAttraction, rPos, dirOnDirectrix, generatrixTermination);
    }

    if (MR::isNearZero(pointOfAttraction - rPos)) {
        Vec3f generatrixDirection = apex - dirOnDirectrix;
        MR::normalizeOrZero(&generatrixDirection);

        Vec3f gravity;
        gravity.killElement((-positionOnBasePlane), generatrixDirection);

        if (MR::isNearZero(gravity)) {
            *pDest = -unitWorldCentralAxis;
        } else {
            MR::normalize(gravity, pDest);
        }
        *pScalar = 0.0f;

        return true;
    }

    if (isInsideCone) {
        MR::normalize(rPos - pointOfAttraction, pDest);
        *pScalar = 0.0f;
        return true;
    }

    return calcGravityFromMassPosition(pDest, pScalar, rPos, pointOfAttraction);
}
