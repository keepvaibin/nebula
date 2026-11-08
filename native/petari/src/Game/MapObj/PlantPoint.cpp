#include "Game/MapObj/PlantPoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>

PlantPoint::PlantPoint(const Vec3f& rPosition, const Vec3f& rUp, f32 thickness)
    : mPosition(rPosition), mSide(1.0f, 0.0f, 0.0f), mUp(rUp), mFront(0.0f, 0.0f, 1.0f), mThickness(thickness) {
    Vec3f front = mSide.cross(mUp);

    if (MR::isNearZero(front)) {
        MR::makeAxisUpFront(&mSide, &mFront, mUp, mFront);
    } else {
        MR::makeAxisUpSide(&mFront, &mSide, mUp, mSide);
    }
}

void PlantPoint::setAxisAndPos(const Vec3f& rSide, const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rPosition) {
    mSide.set(rSide);
    mUp.set(rUp);
    mFront.set(rFront);
    mPosition.set(rPosition);
}
