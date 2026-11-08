#include "Game/Util/SortUtil.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <algorithm>

namespace {
    // hPrintFixReactionLength
    const f32 hOverlapAddValue = 1.2f;
};  // namespace

Binder::Binder(MtxPtr pHostMtx, const Vec3f* pHostPos, const Vec3f* pGravity, f32 radius, f32 offsetY, u32 planeCapacity)
    : mTriangleFilter(), mCollisionPartsFilter(), mExCollisionParts(), mHostMtx(pHostMtx), mHostPos(pHostPos), mGravity(pGravity), mRadius(radius),
      mOffsetY(offsetY), mOffsetVec(), mPlaneCapacity(planeCapacity), mPlaneNum(), mPlane(planeCapacity != 0 ? new HitInfo[planeCapacity] : nullptr),
      mFixReactionVector(0, 0, 0), mGroundInfo(), mGroundDepth(), mWallInfo(), mWallDepth(), mRoofInfo(), mRoofDepth(), mIsMoveWithCollision(true),
      mIsMovingReaction(true), mIsExCollisionValid(false), mIsSkipFirstCheck(false), mIsOffsetInHostFrame(false), mIsStopAtFirstHit(false) {
    clear();
}

void Binder::setTriangleFilter(TriangleFilterBase* pFilter) {
    mTriangleFilter = pFilter;
}

void Binder::setCollisionPartsFilter(CollisionPartsFilterBase* pFilter) {
    mCollisionPartsFilter = pFilter;
}

void Binder::clear() {
    mPlaneNum = 0;
    mGroundDepth = -99999.0f;
    mWallDepth = -99999.0f;
    mRoofDepth = -99999.0f;
    mFixReactionVector.zero();
}

const HitInfo* Binder::getPlane(int index) const {
    return &mPlane[index];
}

u32 Binder::copyPlaneArrayAndSortingSensor(HitInfo** pPlanes, u32 capacity) {
    if (mPlaneCapacity == 0) {
        u32 count = 0;
        if (isBindedGround()) {
            pPlanes[count++] = &mGroundInfo;
        }

        if (isBindedWall()) {
            pPlanes[count++] = &mWallInfo;
        }

        if (isBindedRoof()) {
            pPlanes[count++] = &mRoofInfo;
        }

        MR::sort(pPlanes, pPlanes + count, compSensor);
        return count;
    }

    for (u32 i = 0; i < mPlaneNum; i++) {
        pPlanes[i] = &mPlane[i];
    }

    MR::sort(pPlanes, pPlanes + mPlaneNum, compSensor);
    return mPlaneNum;
}

bool Binder::compSensor(const HitInfo* pPlane1, const HitInfo* pPlane2) {
    return pPlane1->mParentTriangle.mSensor > pPlane2->mParentTriangle.mSensor;
}

// takes the desired movement and returns the collision-adjusted displacement
// collision sphere starts from the host position + binder offset, transformed by the host matrix when applicable
// checks our movement in small steps, pushes the sphere out of overlapping geometry, then slides the remaining movement along the contact surface
// records ground, wall, and roof contacts, stores our push-out vector, and optionally follows moving ground before returning the result
namespace {
    // Binders move on the game thread, one at a time.
    HitInfo sLocalPlanes[32];
}  // namespace

const Vec3f Binder::bind(const Vec3f& rVelocity) {
    const bool stopAtFirstHit = mIsStopAtFirstHit != 0;
    mIsStopAtFirstHit = false;
    clear();

    Vec3f position(*mHostPos);
    Vec3f movement(rVelocity);
    Vec3f result;

    if (mIsExCollisionValid && mExCollisionParts) {
        MR::getCollisionDirector()->getCategoryKeeper(0)->addToGlobal(mExCollisionParts);
    }

    if (mOffsetVec) {
        if (mIsOffsetInHostFrame && mHostMtx) {
            position.x += mHostMtx[0][0] * mOffsetVec->x;
            position.y += mHostMtx[1][0] * mOffsetVec->x;
            position.z += mHostMtx[2][0] * mOffsetVec->x;
            position.x += mHostMtx[0][1] * mOffsetVec->y;
            position.y += mHostMtx[1][1] * mOffsetVec->y;
            position.z += mHostMtx[2][1] * mOffsetVec->y;
            position.x += mHostMtx[0][2] * mOffsetVec->z;
            position.y += mHostMtx[1][2] * mOffsetVec->z;
            position.z += mHostMtx[2][2] * mOffsetVec->z;
        } else {
            position.x += mOffsetVec->x;
            position.y += mOffsetVec->y;
            position.z += mOffsetVec->z;
        }
    } else if (mHostMtx) {
        position.x += mHostMtx[0][1] * mOffsetY;
        position.y += mHostMtx[1][1] * mOffsetY;
        position.z += mHostMtx[2][1] * mOffsetY;
    } else {
        position.y += mOffsetY;
    }

    Vec3f startPosition(position);
    u32 capacity = mPlaneCapacity;
    HitInfo* planes;
    if (capacity == 0) {
        planes = sLocalPlanes;
        capacity = 32;
    } else {
        planes = mPlane;
    }

    bool hasRemainingMovement;
    u32 firstHitCount = findBindedPos(&position, &movement, &hasRemainingMovement, planes, capacity, false, stopAtFirstHit);
    if (firstHitCount == 0) {
        result = movement;
    } else {
        Vec3f firstReaction(0, 0, 0);
        Vec3f reaction(0, 0, 0);
        Vec3f slideReaction(0, 0, 0);
        obtainMomentFixReaction(planes, capacity, &firstReaction, 0);
        position.add(firstReaction);
        Vec3f previousReaction(firstReaction);

        while (!stopAtFirstHit && hasRemainingMovement == true) {
            Vec3f remaining(rVelocity - movement);
            moveAlongHittedPlanes(&movement, &position, &remaining, rVelocity, previousReaction, planes, capacity, &hasRemainingMovement);
            obtainMomentFixReaction(planes, capacity, &reaction, firstHitCount);
            position.add(reaction);
            previousReaction.set(reaction);
            slideReaction.add(reaction);
            hasRemainingMovement = false;
        }

        storeContactPlane(planes, capacity);
        if (mIsMoveWithCollision) {
            moveWithCollisionParts(&position, &movement);
        }

        mFixReactionVector = firstReaction + slideReaction;
        result = position - startPosition;
    }

    if (mIsExCollisionValid && mExCollisionParts) {
        MR::getCollisionDirector()->getCategoryKeeper(0)->removeFromGlobal(mExCollisionParts);
    }

    return result;
}

void Binder::moveAlongHittedPlanes(Vec3f* pMovement, Vec3f* pPosition, Vec3f* pRemaining, const Vec3f& rVelocity, const Vec3f& rReaction,
                                   HitInfo* pPlanes, u32 capacity, bool* pHasRemainingMovement) {
    Vec3f normal(rReaction);
    MR::normalizeOrZero(&normal);
    f32 dot = pRemaining->dot(normal);
    if (dot < 0.0f) {
        pRemaining->sub(normal * dot);
    }

    if (rVelocity.dot(*pRemaining) < 0.0f) {
        *pHasRemainingMovement = false;
    } else {
        findBindedPos(pPosition, pRemaining, pHasRemainingMovement, pPlanes, capacity, true, false);
        pMovement->add(*pRemaining);
    }
}

u32 Binder::findBindedPos(Vec3f* pPosition, Vec3f* pMovement, bool* pHasRemainingMovement, HitInfo* pPlanes, u32 capacity, bool skipInitial,
                          bool stopAtFirstHit) {
    s32 steps = static_cast< s32 >((1.0f / 35.0f) * pMovement->length()) + 1;
    Vec3f step(*pMovement);
    if (steps > 1) {
        step /= steps;
    }

    pMovement->zero();
    for (s32 i = 0; i <= steps; i++) {
        if (i != 0) {
            pPosition->add(step);
            pMovement->add(step);
        } else if (mIsSkipFirstCheck || skipInitial) {
            continue;
        }

        s32 hitCount;
        if (mIsMovingReaction) {
            hitCount = Collision::checkStrikeBallToMapWithMovingReaction(*pPosition, mRadius, mCollisionPartsFilter, mTriangleFilter);
        } else {
            hitCount = Collision::checkStrikeBallToMap(*pPosition, mRadius, mCollisionPartsFilter, mTriangleFilter);
        }

        if (hitCount != 0) {
            if (i == steps) {
                *pHasRemainingMovement = false;
            } else {
                *pHasRemainingMovement = true;
            }

            return storeCurrentHitInfo(pPlanes, capacity, stopAtFirstHit);
        }
    }

    *pHasRemainingMovement = false;
    return 0;
}

bool Binder::moveWithCollisionParts(Vec3f* pPosition, Vec3f* pMovement) {
    if (mGroundDepth < 0.0f) {
        return false;
    }

    if (!mGroundInfo.mParentTriangle.isHostMoved()) {
        return false;
    }

    Vec3f force;
    mGroundInfo.mParentTriangle.calcForceMovePower(&force, *pPosition);
    pMovement->add(force);
    pPosition->add(force);
    return true;
}

u32 Binder::storeCurrentHitInfo(HitInfo* pPlanes, u32 capacity, bool stopAtFirstHit) {
    u32 hitCount = Collision::getStrikeInfoNumMap();
    u32 count = 0;
    for (u32 i = 0; i < hitCount; i++) {
        if (capacity <= count + mPlaneNum) {
            u32 stored = capacity - mPlaneNum;
            mPlaneNum = capacity;
            return stored;
        }

        pPlanes[count + mPlaneNum] = *Collision::getStrikeInfoMap(i);
        if (!stopAtFirstHit) {
            pPlanes[i + mPlaneNum].mDepth += ::hOverlapAddValue;
        }

        count++;
    }

    mPlaneNum += count;
    return count;
}

void Binder::obtainMomentFixReaction(HitInfo* pPlanes, u32 capacity, Vec3f* pReaction, u32 start) {
    Vec3f positive(0, 0, 0);
    Vec3f negative(0, 0, 0);
    for (u32 i = start; i < mPlaneNum; i++) {
        HitInfo& plane = pPlanes[i];
        Vec3f normal(*plane.mParentTriangle.getNormal(0));
        f32 x = normal.x * plane.mDepth;
        if (positive.x < x) {
            positive.x = x;
        } else if (x < negative.x) {
            negative.x = x;
        }

        f32 y = normal.y * plane.mDepth;
        if (positive.y < y) {
            positive.y = y;
        } else if (y < negative.y) {
            negative.y = y;
        }

        f32 z = normal.z * plane.mDepth;
        if (positive.z < z) {
            positive.z = z;
        } else if (z < negative.z) {
            negative.z = z;
        }

        if (mIsMovingReaction && !MR::isNearZero(plane.mNormalMove)) {
            x = plane.mNormalMove.x;
            if (positive.x < x) {
                positive.x = x;
            } else if (x < negative.x) {
                negative.x = x;
            }

            y = plane.mNormalMove.y;
            if (positive.y < y) {
                positive.y = y;
            } else if (y < negative.y) {
                negative.y = y;
            }

            z = plane.mNormalMove.z;
            if (positive.z < z) {
                positive.z = z;
            } else if (z < negative.z) {
                negative.z = z;
            }
        }
    }

    pReaction->set(positive);
    pReaction->add(negative);
}

void Binder::storeContactPlane(HitInfo* pPlanes, u32 capacity) {
    for (u32 i = 0; i < mPlaneNum; i++) {
        const Vec3f& normal = *pPlanes[i].mParentTriangle.getNormal(0);
        if (MR::isFloorPolygon(normal, *mGravity)) {
            if (mGroundDepth < pPlanes[i].mDepth) {
                mGroundInfo = pPlanes[i];
                mGroundDepth = pPlanes[i].mDepth;
            }
        } else if (MR::isWallPolygon(normal, *mGravity)) {
            if (mWallDepth < pPlanes[i].mDepth) {
                mWallInfo = pPlanes[i];
                mWallDepth = pPlanes[i].mDepth;
            }
        } else {
            if (mRoofDepth < pPlanes[i].mDepth) {
                mRoofInfo = pPlanes[i];
                mRoofDepth = pPlanes[i].mDepth;
            }
        }
    }
}
