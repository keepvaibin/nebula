#include "Game/MapObj/PlantPoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>

PlantPoint::PlantPoint(const Vec3f& pPosition, const Vec3f& pUp, f32 thickness)
    : mPosition(pPosition.x, pPosition.y, pPosition.z), mSide(1.0f, 0.0f, 0.0f), mUp(pUp), mFront(0.0f, 0.0f, 1.0f), mThickness(thickness) {
    Vec3f front = mSide.cross(mUp);

    if (MR::isNearZero(front)) {
        MR::makeAxisUpFront(&mSide, &mFront, mUp, mFront);
    } else {
        MR::makeAxisUpSide(&mFront, &mSide, mUp, mSide);
    }
}

void PlantPoint::setAxisAndPos(const Vec3f& pSide, const Vec3f& pUp, const Vec3f& pFront, const Vec3f& pPosition) {
    mSide.set(pSide);
    mUp.set(pUp);
    mFront.set(pFront);
    mPosition.set(pPosition);
}
