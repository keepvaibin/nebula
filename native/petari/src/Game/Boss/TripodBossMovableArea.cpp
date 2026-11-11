#include "Game/Boss/TripodBossMovableArea.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

namespace {
    // The unit vector along the axis where rAxis is smallest.
    inline void setLeastAxis(Vec3f* pVec, const Vec3f& rAxis) {
        pVec->zero();
        switch (MR::getMinAbsElementIndex(rAxis)) {
        case 0:
            pVec->x = 1.0f;
            break;
        case 1:
            pVec->y = 1.0f;
            break;
        default:
            pVec->z = 1.0f;
            break;
        }
    }
};  // namespace

HitResult::HitResult() : mHitPos(0.0f, 0.0f, 0.0f), mPushedPos(0.0f, 0.0f, 0.0f), mNormal(0.0f, 1.0f, 0.0f) {
}

TripodBossMovableArea::TripodBossMovableArea()
    : mCenter(0.0f, 0.0f, 0.0f), mBaseAxis(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mRadius(3000.0f), mRangeMin(-1.0f), mRangeMax(1.0f) {
}

void TripodBossMovableArea::setCenter(const Vec3f& rCenter) {
    mCenter = rCenter;
}

void TripodBossMovableArea::setBaseAxis(const Vec3f& rAxis) {
    mBaseAxis = rAxis;
    MR::normalizeOrZero(&mBaseAxis);
}

// mFront is stored as given; the base axis is normalized again.
void TripodBossMovableArea::setFrontVector(const Vec3f& rFront) {
    mFront = rFront;
    MR::normalizeOrZero(&mBaseAxis);
}

void TripodBossMovableArea::setRadius(f32 radius) {
    mRadius = radius;
}

// A sphere of radius from rStart moving by rMove onto the area sphere from outside: mHitPos is where it
// touches, mNormal the outward normal there, mPushedPos the move slid along the surface.
bool TripodBossMovableArea::collideSphere(HitResult* pResult, const Vec3f& rStart, f32 radius, const Vec3f& rMove) const {
    pResult->mPushedPos = rStart + rMove;
    f32 hitRadius = mRadius + radius;
    Vec3f fromCenter(rStart);
    fromCenter -= mCenter;
    Vec3f dir(rMove);
    f32 moveLength;
    MR::separateScalarAndDirection(&moveLength, &dir, dir);
    f32 along = fromCenter.dot(dir);
    f32 distSq = fromCenter.dot(fromCenter);

    f32 outside = distSq - (hitRadius * hitRadius);

    if (along > 0.0f && outside > 0.0f) {
        return false;
    }

    f32 disc = ((along * along) - outside);
    if (disc < 0.0f) {
        return false;
    }

    f32 hitDist = (-along - MR::sqrt< f32 >(disc));
    if (hitDist > moveLength) {
        return false;
    }

    if (hitDist < 0.0f) {
        hitDist = 0.0f;
    }

    pResult->mHitPos = rStart + (dir * hitDist);
    Vec3f normal(pResult->mHitPos);
    normal -= mCenter;
    f32 hitCenterDist;
    MR::separateScalarAndDirection(&hitCenterDist, &normal, normal);
    if (hitCenterDist < hitRadius) {
        if (MR::isNearZero(hitCenterDist)) {
            normal.set< int >(0, 1, 0);
        }

        pResult->mHitPos = mCenter + (normal * hitRadius);
    }

    pResult->mNormal = normal;
    Vec3f slide(pResult->mPushedPos);
    slide -= pResult->mHitPos;
    f32 into = slide.dot(pResult->mNormal);
    slide -= pResult->mNormal * into;
    pResult->mPushedPos = pResult->mHitPos + slide;
    return true;
}

// The surface point toward rPos, its base-axis cosine held to mRangeMin..mRangeMax.
// The clamped branch returns it relative to mCenter.
void TripodBossMovableArea::calcNearLandingPosition(Vec3f* pPos, const Vec3f& rPos) const {
    f32 dist;
    Vec3f dir(rPos);
    dir -= mCenter;
    MR::separateScalarAndDirection(&dist, &dir, dir);

    if (MR::isNearZero(dist)) {
        dir = mBaseAxis;
    }

    f32 axisCos = mBaseAxis.dot(dir);
    if (MR::isInRange(axisCos, mRangeMin, mRangeMax)) {
        pPos->set(mCenter + (dir * mRadius));
    } else {
        if (axisCos < mRangeMin) {
            axisCos = mRangeMin;
        } else if (axisCos > mRangeMax) {
            axisCos = mRangeMax;
        }

        Vec3f side;
        side.killElement2(dir, mBaseAxis);
        if (MR::isNearZero(side)) {
            setLeastAxis(&side, mBaseAxis);
        }

        MR::normalizeOrZero(&side);
        f32 axisSin = MR::sqrt< f32 >((1.0f - (axisCos * axisCos)));
        Vec3f surfaceDir((mBaseAxis * axisCos) + (side * axisSin));
        pPos->set(surfaceDir * mRadius);
    }
}

void TripodBossMovableArea::calcLandingNormal(Vec3f* pNorm, const Vec3f& rPos) const {
    Vec3f norm = rPos - mCenter;
    MR::normalizeOrZero(&norm);
    if (MR::isNearZero(norm)) {
        norm = mBaseAxis;
    }

    pNorm->set(norm);
}

// Down the base axis, across the surface normal at rPos.
void TripodBossMovableArea::calcLandingFront(Vec3f* pFront, const Vec3f& rPos) const {
    Vec3f norm = rPos - mCenter;
    MR::normalizeOrZero(&norm);
    Vec3f front = (-mBaseAxis).killElement(norm);
    MR::normalizeOrZero(&front);
    if (MR::isNearZero(front)) {
        setLeastAxis(&front, mBaseAxis);
        front.orthogonalize2(mBaseAxis);
        MR::normalizeOrZero(&front);
    }

    pFront->set(front);
}
