#include "Game/Util/SortUtil.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <algorithm>

namespace {
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

// Without a plane list: the ground, wall and roof planes (capacity 3 or more). With one: its planes, up to capacity.
u32 Binder::copyPlaneArrayAndSortingSensor(HitInfo** pPlanes, u32 capacity) {
    u32 count = 0;
    if (mPlaneCapacity == 0) {
        if (isBindedGround()) {
            pPlanes[count++] = &mGroundInfo;
        }

        if (isBindedWall()) {
            pPlanes[count++] = &mWallInfo;
        }

        if (isBindedRoof()) {
            pPlanes[count++] = &mRoofInfo;
        }
    } else {
        for (; count < mPlaneNum && count < capacity; count++) {
            pPlanes[count] = &mPlane[count];
        }
    }

    MR::sort(pPlanes, pPlanes + count, compSensor);
    return count;
}

bool Binder::compSensor(const HitInfo* pPlane1, const HitInfo* pPlane2) {
    return pPlane1->mParentTriangle.mSensor > pPlane2->mParentTriangle.mSensor;
}

// takes the desired movement and returns the collision-adjusted displacement
// collision sphere starts from the host position + binder offset, transformed by the host matrix when applicable
// checks our movement in small steps, pushes the sphere out of overlapping geometry, then slides the remaining movement along the contact surface
// records ground, wall, and roof contacts, stores our push-out vector, and optionally follows moving ground before returning the result
namespace {
    // Binders move on the game thread, one at a time. A bind without a plane list of its own keeps every plane it meets
    // here.
    MR::GrowBuffer< HitInfo > sLocalPlanes;
}  // namespace

HitInfo* Binder::planes() {
    return mPlaneCapacity != 0 ? mPlane : sLocalPlanes.mItems;
}

HitInfo* Binder::planeRoom(u32 count) {
    if (mPlaneCapacity == 0) {
        return sLocalPlanes.room(count);
    }
    if (count > mPlaneCapacity) {
        u32 room = mPlaneCapacity * 2;
        while (room < count) {
            room *= 2;
        }
        HitInfo* const pNew = new (NebulaArena::findFromRoot(mPlane), 0) HitInfo[room];
        for (u32 i = 0; i < mPlaneNum; i++) {
            pNew[i] = mPlane[i];
        }
        delete[] mPlane;
        mPlane = pNew;
        mPlaneCapacity = room;
    }
    return mPlane;
}

const Vec3f Binder::bind(const Vec3f& rVelocity) {
    const bool stopAtFirstHit = mIsStopAtFirstHit != 0;
    mIsStopAtFirstHit = false;
    clear();

    Vec3f position(*mHostPos);
    Vec3f movement(rVelocity);
    Vec3f result;

    if (mIsExCollisionValid && mExCollisionParts) {
        MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->addToGlobal(mExCollisionParts);
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
    bool hasRemainingMovement;
    u32 firstHitCount = findBindedPos(&position, &movement, &hasRemainingMovement, false, stopAtFirstHit);
    if (firstHitCount == 0) {
        result = movement;
    } else {
        Vec3f firstReaction(0, 0, 0);
        Vec3f reaction(0, 0, 0);
        Vec3f slideReaction(0, 0, 0);
        obtainMomentFixReaction(&firstReaction, 0);
        position.add(firstReaction);
        Vec3f previousReaction(firstReaction);

        while (!stopAtFirstHit && hasRemainingMovement) {
            Vec3f remaining(rVelocity - movement);
            moveAlongHittedPlanes(&movement, &position, &remaining, rVelocity, previousReaction, &hasRemainingMovement);
            obtainMomentFixReaction(&reaction, firstHitCount);
            position.add(reaction);
            previousReaction.set(reaction);
            slideReaction.add(reaction);
            hasRemainingMovement = false;
        }

        storeContactPlane();
        if (mIsMoveWithCollision) {
            moveWithCollisionParts(&position, &movement);
        }

        mFixReactionVector = firstReaction + slideReaction;
        result = position - startPosition;
    }

    if (mIsExCollisionValid && mExCollisionParts) {
        MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->removeFromGlobal(mExCollisionParts);
    }

    return result;
}

void Binder::moveAlongHittedPlanes(Vec3f* pMovement, Vec3f* pPosition, Vec3f* pRemaining, const Vec3f& rVelocity, const Vec3f& rReaction,
                                   bool* pHasRemainingMovement) {
    Vec3f normal(rReaction);
    MR::normalizeOrZero(&normal);
    f32 dot = pRemaining->dot(normal);
    if (dot < 0.0f) {
        pRemaining->sub(normal * dot);
    }

    if (rVelocity.dot(*pRemaining) < 0.0f) {
        *pHasRemainingMovement = false;
    } else {
        findBindedPos(pPosition, pRemaining, pHasRemainingMovement, true, false);
        pMovement->add(*pRemaining);
    }
}

u32 Binder::findBindedPos(Vec3f* pPosition, Vec3f* pMovement, bool* pHasRemainingMovement, bool skipInitial, bool stopAtFirstHit) {
    // One check per 35 units of movement; a movement of 35000 or more, or not a number, takes 1000 (it would not end).
    const f32 movementLength = pMovement->length();
    s32 steps = movementLength < 35000.0f ? static_cast< s32 >((1.0f / 35.0f) * movementLength) + 1 : 1000;
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

            return storeCurrentHitInfo(stopAtFirstHit);
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

u32 Binder::storeCurrentHitInfo(bool stopAtFirstHit) {
    const u32 hitCount = Collision::getStrikeInfoNumMap();
    HitInfo* const pPlanes = planeRoom(mPlaneNum + hitCount);
    for (u32 i = 0; i < hitCount; i++) {
        pPlanes[mPlaneNum + i] = *Collision::getStrikeInfoMap(i);
        if (!stopAtFirstHit) {
            pPlanes[mPlaneNum + i].mDepth += ::hOverlapAddValue;
        }
    }

    mPlaneNum += hitCount;
    return hitCount;
}

void Binder::obtainMomentFixReaction(Vec3f* pReaction, u32 start) {
    const HitInfo* const pPlanes = planes();
    Vec3f positive(0, 0, 0);
    Vec3f negative(0, 0, 0);
    for (u32 i = start; i < mPlaneNum; i++) {
        const HitInfo& plane = pPlanes[i];
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

void Binder::storeContactPlane() {
    const HitInfo* const pPlanes = planes();
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
