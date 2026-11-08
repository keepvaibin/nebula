#include "Game/Enemy/TerritoryMover.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

TerritoryMover::TerritoryMover(f32 radius) : mRadius(radius) {
}

void TerritoryMover::decideNextTargetPos(const LiveActor* pActor) {
    Vec3f randVec;
    MR::getRandomVector(&randVec, 1.0f);
    MR::normalizeOrZero(&randVec);

    randVec.orthogonalize(pActor->mGravity);
    randVec *= mRadius;
    randVec += mCenter;
    mTarget = randVec;
}

bool TerritoryMover::isReachedTarget(const LiveActor* pActor, f32 range) {
    Vec3f planar;
    Vec3f diff = mTarget - pActor->mPosition;
    planar.killElement(diff, pActor->mGravity);
    return planar.length() < range;
}
