#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util/CollisionPartsFilter.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcMinMaxAndRadius_f1, "calcMinMaxAndRadius.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAndUpdateMinMax_f1, "addAndUpdateMinMax.f1", 0.1f);

CollisionCategorizedKeeper::CollisionCategorizedKeeper(s32 category)
    : NameObj("地形コリジョンカテゴリキーパー"), mHitInfoArray(), mNumHits(), mPartsCount(), mCategory(category), mZonesInitialized(),
      mUpdateZoneBounds(true) {
    lumacore::setClass(this, lumacore::kClassId< CollisionCategorizedKeeper >);
    mHitInfoRoom = 32;
    mHitInfoArray = new HitInfo[mHitInfoRoom];
}

void CollisionCategorizedKeeper::movement() {
    CollisionParts* pParts;

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        s32 count = (*pZone)->mParts.size();
#if defined(LUMA_HOOK)
        // The module with hook points (its copy of this file starts with luma_hook.h) makes the game's calls.
        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (pParts->mIsValid) {
                if (mCategory == pParts->mKeeperIndex) {
                    pParts->updateMtx();
                }

                if (mUpdateZoneBounds) {
                    (*pZone)->calcMinMaxAndRadius();
                } else if (pParts->mStillCount == 0) {
                    (*pZone)->calcMinMaxAndRadiusIfMoveOuter(pParts);
                }
            }
        }
#else
        // The global zone (the first) has no bounds: every query takes its parts without a zone test, and addParts
        // never makes them. The others' first bounds are made once, from every part's new matrix (as the last of a
        // remake per valid part left them).
        const bool bounded = pZone != mZones.begin();
        bool remake = false;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (pParts->mIsValid) {
                if (mCategory == pParts->mKeeperIndex) {
                    pParts->updateMtx();
                }

                if (!bounded) {
                    continue;
                }

                if (mUpdateZoneBounds) {
                    remake = true;
                } else if (pParts->mStillCount == 0) {
                    (*pZone)->calcMinMaxAndRadiusIfMoveOuter(pParts);
                }
            }
        }

        if (remake) {
            (*pZone)->calcMinMaxAndRadius();
        }
#endif
    }

    mUpdateZoneBounds = false;
}

void CollisionCategorizedKeeper::addToZone(CollisionParts* pParts, s32 zone) {
    mZones[zone]->addParts(pParts);
    mPartsCount++;
}

void CollisionCategorizedKeeper::removeFromZone(CollisionParts* pParts, s32 zone) {
    mZones[zone]->eraseParts(pParts);
    mPartsCount--;
}

void CollisionCategorizedKeeper::addToGlobal(CollisionParts* pParts) {
    mZones[0]->addParts(pParts);
    mPartsCount++;
}

void CollisionCategorizedKeeper::removeFromGlobal(CollisionParts* pParts) {
    mZones[0]->eraseParts(pParts);
    mPartsCount--;
}

s32 CollisionCategorizedKeeper::checkStrikePoint(const Vec3f& rPosition, HitInfo* pHitInfo) {
    mNumHits = 0;

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        if (pZone != mZones.begin()) {
            if (!isSphereOverlappingWithBox((*pZone)->mMin, (*pZone)->mMax, rPosition, 0.0f)) {
                continue;
            }

            f32 radius = (*pZone)->mRadius;
            Vec3f offset((*pZone)->mCenter);
            offset -= rPosition;

            if (radius * radius < offset.squared()) {
                continue;
            }
        }

        int count = (*pZone)->mParts.size();
        CollisionParts* pParts;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (!pParts->mIsValid) {
                continue;
            }

            f32 radius = pParts->mBoundingRadius;
            Vec3f offset;
            offset.x = MR::abs(pParts->getTrans().x - rPosition.x);

            if (radius < offset.x) {
                continue;
            }

            offset.y = MR::abs(pParts->getTrans().y - rPosition.y);

            if (radius < offset.y) {
                continue;
            }

            offset.z = MR::abs(pParts->getTrans().z - rPosition.z);

            if (radius < offset.z) {
                continue;
            }

            if (offset.squared() > radius * radius) {
                continue;
            }

            if (pParts->checkStrikePoint(pHitInfo, rPosition)) {
                mNumHits = 1;
                return 1;
            }
        }
    }

    return mNumHits;
}

s32 CollisionCategorizedKeeper::collectStrikeBall(const Vec3f& rPosition, f32 ballRadius, bool moving, const CollisionPartsFilterBase* pPartsFilter,
                                                const TriangleFilterBase* pTriangleFilter) {
    mNumHits = 0;
    s32 hitCount = 0;

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        if (pZone != mZones.begin()) {
            if (!isSphereOverlappingWithBox((*pZone)->mMin, (*pZone)->mMax, rPosition, ballRadius)) {
                continue;
            }

            f32 radius = ballRadius + (*pZone)->mRadius;
            Vec3f offset((*pZone)->mCenter);
            offset -= rPosition;

            if (radius * radius < offset.squared()) {
                continue;
            }
        }

        s32 count = (*pZone)->mParts.size();
        CollisionParts* pParts;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (!pParts->mIsValid) {
                continue;
            }

            if (pPartsFilter != nullptr && pPartsFilter->isInvalidParts(pParts)) {
                continue;
            }

            f32 radius = ballRadius + pParts->mBoundingRadius;
            Vec3f offset;
            offset.x = MR::abs(pParts->getTrans().x - rPosition.x);

            if (radius < offset.x) {
                continue;
            }

            offset.y = MR::abs(pParts->getTrans().y - rPosition.y);

            if (radius < offset.y) {
                continue;
            }

            offset.z = MR::abs(pParts->getTrans().z - rPosition.z);

            if (radius < offset.z) {
                continue;
            }

            if (offset.squared() > radius * radius) {
                continue;
            }

            hitCount += pParts->checkStrikeBall(&mHitInfoArray[hitCount], mHitInfoRoom - hitCount, rPosition, ballRadius, moving, pTriangleFilter);

            if (hitCount >= mHitInfoRoom) {
                mNumHits = hitCount;
                return hitCount;
            }
        }
    }

    mNumHits = hitCount;
    return hitCount;
}

s32 CollisionCategorizedKeeper::collectStrikeBallWithThickness(const Vec3f& rPosition, f32 ballRadius, f32 thickness,
                                                             const CollisionPartsFilterBase* pPartsFilter,
                                                             const TriangleFilterBase* pTriangleFilter) {
    mNumHits = 0;
    s32 hitCount = 0;

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        if (pZone != mZones.begin()) {
            if (!isSphereOverlappingWithBox((*pZone)->mMin, (*pZone)->mMax, rPosition, ballRadius)) {
                continue;
            }

            f32 radius = ballRadius + (*pZone)->mRadius;
            Vec3f offset((*pZone)->mCenter);
            offset -= rPosition;

            if (radius * radius < offset.squared()) {
                continue;
            }
        }

        s32 count = (*pZone)->mParts.size();
        CollisionParts* pParts;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (!pParts->mIsValid) {
                continue;
            }

            if (pPartsFilter != nullptr && pPartsFilter->isInvalidParts(pParts)) {
                continue;
            }

            f32 radius = ballRadius + pParts->mBoundingRadius;
            Vec3f offset;
            offset.x = MR::abs(pParts->getTrans().x - rPosition.x);

            if (radius < offset.x) {
                continue;
            }

            offset.y = MR::abs(pParts->getTrans().y - rPosition.y);

            if (radius < offset.y) {
                continue;
            }

            offset.z = MR::abs(pParts->getTrans().z - rPosition.z);

            if (radius < offset.z) {
                continue;
            }

            if (offset.squared() > radius * radius) {
                continue;
            }

            hitCount +=
                pParts->checkStrikeBallWithThickness(&mHitInfoArray[hitCount], mHitInfoRoom - hitCount, rPosition, ballRadius, thickness, pTriangleFilter);

            if (hitCount >= mHitInfoRoom) {
                mNumHits = hitCount;
                return hitCount;
            }
        }
    }

    mNumHits = hitCount;
    return hitCount;
}

s32 CollisionCategorizedKeeper::collectStrikeLine(const Vec3f& rStart, const Vec3f& rDirection, s32 maxHits,
                                                const CollisionPartsFilterBase* pPartsFilter, const TriangleFilterBase* pTriangleFilter) {
    if (maxHits == 0 || maxHits > mHitInfoRoom) {
        maxHits = mHitInfoRoom;
    }

    mNumHits = 0;
    s32 hitCount = 0;
    Vec3f min(rStart);
    Vec3f max(rStart);

    if (rDirection.x < 0.0f) {
        min.x += rDirection.x;
    } else {
        max.x += rDirection.x;
    }

    if (rDirection.y < 0.0f) {
        min.y += rDirection.y;
    } else {
        max.y += rDirection.y;
    }

    if (rDirection.z < 0.0f) {
        min.z += rDirection.z;
    } else {
        max.z += rDirection.z;
    }

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        if (pZone != mZones.begin()) {
            Vec3f position((*pZone)->mCenter);
            f32 radius = (*pZone)->mRadius;

            if (!isSphereOverlappingWithBox(min, max, position, radius)) {
                continue;
            }

            if (!MR::checkHitSegmentSphere(position, rStart, rStart + rDirection, radius, nullptr)) {
                continue;
            }
        }

        s32 count = (*pZone)->mParts.size();
        CollisionParts* pParts;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (!pParts->mIsValid) {
                continue;
            }

            if (pPartsFilter != nullptr && pPartsFilter->isInvalidParts(pParts)) {
                continue;
            }

            Vec3f position(pParts->getTrans());
            f32 radius = pParts->mBoundingRadius;

            if (!isSphereOverlappingWithBox(min, max, position, radius)) {
                continue;
            }

            if (!MR::checkHitSegmentSphere(position, rStart, rStart + rDirection, radius, nullptr)) {
                continue;
            }

            hitCount += pParts->checkStrikeLine(&mHitInfoArray[hitCount], maxHits - hitCount, rStart, rDirection, pTriangleFilter);

            if (maxHits <= hitCount) {
                mNumHits = hitCount;
                return hitCount;
            }
        }
    }

    mNumHits = hitCount;
    return hitCount;
}

u32 CollisionCategorizedKeeper::createAreaPolygonList(Triangle* pTriangles, u32 maxTriangles, const Vec3f& rStart, const Vec3f& rEnd) {
    u32 triangleCount = 0;
    Vec3f min;
    Vec3f max;

    if (rStart.x < rEnd.x) {
        min.x = rStart.x;
        max.x = rEnd.x;
    } else {
        min.x = rEnd.x;
        max.x = rStart.x;
    }

    if (rStart.y < rEnd.y) {
        min.y = rStart.y;
        max.y = rEnd.y;
    } else {
        min.y = rEnd.y;
        max.y = rStart.y;
    }

    if (rStart.z < rEnd.z) {
        min.z = rStart.z;
        max.z = rEnd.z;
    } else {
        min.z = rEnd.z;
        max.z = rStart.z;
    }

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        if (pZone != mZones.begin() && !isSphereOverlappingWithBox(min, max, (*pZone)->mCenter, (*pZone)->mRadius)) {
            continue;
        }

        s32 count = (*pZone)->mParts.size();
        CollisionParts* pParts;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (!pParts->mIsValid) {
                continue;
            }

            f32 radius = pParts->mBoundingRadius;
            Vec3f position(pParts->getTrans());

            if (!isSphereOverlappingWithBox(min, max, position, radius)) {
                continue;
            }

            triangleCount += pParts->createAreaPolygonList(pTriangles + triangleCount, maxTriangles - triangleCount, rStart, rEnd);

            if (maxTriangles <= triangleCount) {
                return triangleCount;
            }
        }
    }

    return triangleCount;
}

u32 CollisionCategorizedKeeper::createAreaPolygonListArray(Triangle* pTriangles, u32 maxTriangles, Vec3f* pVertices, u32 vertexCount) {
    u32 triangleCount = 0;
    Vec3f min;
    Vec3f max;
    MR::createBoundingBox(pVertices, vertexCount, &min, &max);

    for (CollisionZone** pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        if (pZone != mZones.begin() && !isSphereOverlappingWithBox(min, max, (*pZone)->mCenter, (*pZone)->mRadius)) {
            continue;
        }

        s32 count = (*pZone)->mParts.size();
        CollisionParts* pParts;

        for (int i = 0; i < count; i++) {
            pParts = (*pZone)->mParts[i];

            if (!pParts->mIsValid) {
                continue;
            }

            f32 radius = pParts->mBoundingRadius;
            Vec3f position(pParts->getTrans());

            if (!isSphereOverlappingWithBox(min, max, position, radius)) {
                continue;
            }

            triangleCount += pParts->createAreaPolygonListArray(pTriangles + triangleCount, maxTriangles - triangleCount, pVertices, vertexCount);

            if (maxTriangles <= triangleCount) {
                return triangleCount;
            }
        }
    }

    return triangleCount;
}

bool CollisionCategorizedKeeper::isSphereOverlappingWithBox(const Vec3f& rMin, const Vec3f& rMax, const Vec3f& rCenter, f32 radius) {
    if (rCenter.x < rMin.x - radius || rMax.x + radius < rCenter.x) {
        return false;
    }

    if (rCenter.y < rMin.y - radius || rMax.y + radius < rCenter.y) {
        return false;
    }

    if (rCenter.z < rMin.z - radius || rMax.z + radius < rCenter.z) {
        return false;
    }

    return true;
}

bool CollisionCategorizedKeeper::searchSameHostParts(CollisionParts** pResult, CollisionParts* pParts) const {
    for (CollisionZone* const* pZone = mZones.begin(); pZone != mZones.end(); pZone++) {
        s32 count = (*pZone)->mParts.size();

        for (int i = 0; i < count; i++) {
            CollisionParts* pCandidate = (*pZone)->mParts[i];

            if (pCandidate->mHitSensor->mHost == pParts->mHitSensor->mHost) {
                *pResult = pCandidate;
                return true;
            }
        }
    }

    return false;
}

void CollisionCategorizedKeeper::growHitInfo() {
    delete[] mHitInfoArray;
    mHitInfoRoom *= 2;
    mHitInfoArray = new (NebulaArena::findFromRoot(this), 0) HitInfo[mHitInfoRoom];
}

s32 CollisionCategorizedKeeper::checkStrikeBall(const Vec3f& rPosition, f32 ballRadius, bool moving,
                                                const CollisionPartsFilterBase* pPartsFilter,
                                                const TriangleFilterBase* pTriangleFilter) {
    s32 hitCount = collectStrikeBall(rPosition, ballRadius, moving, pPartsFilter, pTriangleFilter);
    while (hitCount >= mHitInfoRoom) {
        growHitInfo();
        hitCount = collectStrikeBall(rPosition, ballRadius, moving, pPartsFilter, pTriangleFilter);
    }

    return hitCount;
}

s32 CollisionCategorizedKeeper::checkStrikeBallWithThickness(const Vec3f& rPosition, f32 ballRadius, f32 thickness,
                                                             const CollisionPartsFilterBase* pPartsFilter,
                                                             const TriangleFilterBase* pTriangleFilter) {
    s32 hitCount = collectStrikeBallWithThickness(rPosition, ballRadius, thickness, pPartsFilter, pTriangleFilter);
    while (hitCount >= mHitInfoRoom) {
        growHitInfo();
        hitCount = collectStrikeBallWithThickness(rPosition, ballRadius, thickness, pPartsFilter, pTriangleFilter);
    }

    return hitCount;
}

s32 CollisionCategorizedKeeper::checkStrikeLine(const Vec3f& rStart, const Vec3f& rDirection, s32 maxHits,
                                                const CollisionPartsFilterBase* pPartsFilter,
                                                const TriangleFilterBase* pTriangleFilter) {
    s32 hitCount = collectStrikeLine(rStart, rDirection, maxHits, pPartsFilter, pTriangleFilter);
    while (hitCount >= mHitInfoRoom && (maxHits == 0 || maxHits > hitCount)) {
        growHitInfo();
        hitCount = collectStrikeLine(rStart, rDirection, maxHits, pPartsFilter, pTriangleFilter);
    }

    return hitCount;
}

HitInfo* CollisionCategorizedKeeper::getStrikeInfo(u32 index) {
    return &mHitInfoArray[index];
}

CollisionZone* CollisionCategorizedKeeper::getZone(int zone) {
    if (!mZonesInitialized) {
        s32 count = MR::getZoneNum();

        for (int i = 0; i < count; i++) {
            mZones.push_back(new CollisionZone(i));
        }

        mZonesInitialized = true;
    }

    // A part made outside any placement (zone -1, e.g. spawned by a mod) or past the zone list belongs to the global zone.
    return mZones[zone >= 0 && zone < mZones.size() ? zone : 0];
}

CollisionZone::CollisionZone(s32 zoneID) : mZoneID(zoneID), mCenter(0, 0, 0), mRadius(), mMin(0, 0, 0), mMax(0, 0, 0) {
}

void CollisionZone::addParts(CollisionParts* pParts) {
    mParts.push_back(pParts);

    if (mZoneID != 0) {
        calcMinMaxAndRadius();
    }
}

void CollisionZone::calcMinMaxAndRadius() {
    CollisionParts** pCurrent;
    mMin.zero();
    mMax.zero();
    mRadius = 0.0f;

    for (CollisionParts** pParts = mParts.begin(); pParts != mParts.end(); pParts++) {
        Vec3f min((*pParts)->getTrans());
        Vec3f max((*pParts)->getTrans());
        f32 radius = (*pParts)->mBoundingRadius;
        min -= Vec3f(radius, radius, radius);
        max += Vec3f(radius, radius, radius);
        addAndUpdateMinMax(min, max);
    }

    mCenter = (mMax + mMin) / cNum_calcMinMaxAndRadius_f1;
    f32 radius = 0.0f;

    for (pCurrent = mParts.begin(); pCurrent != mParts.end(); pCurrent++) {
        Vec3f position((*pCurrent)->getTrans());
        position -= mCenter;
        f32 outer = position.length();
        outer += (*pCurrent)->mBoundingRadius;

        if (radius < outer) {
            radius = outer;
        }
    }

    mRadius = radius;
}

void CollisionZone::calcMinMaxAndRadiusIfMoveOuter(CollisionParts* pParts) {
    f32 radius = pParts->mBoundingRadius;
    Vec3f position(pParts->getTrans());
    Vec3f min(mMin);
    Vec3f max(mMax);
    min.x += radius;
    min.y += radius;
    min.z += radius;
    max.x -= radius;
    max.y -= radius;
    max.z -= radius;

    if (!MR::isInRange(position.x, min.x, max.x) || !MR::isInRange(position.y, min.y, max.y) || !MR::isInRange(position.z, min.z, max.z)) {
        calcMinMaxAndRadius();
    }
}

void CollisionZone::addAndUpdateMinMax(Vec3f min, Vec3f max) {
    if (mRadius == 0.0f) {
        mRadius = cNum_addAndUpdateMinMax_f1;
        mMin.set(min);
        mMax.set(max);
    } else {
        if (min.x < mMin.x) {
            mMin.x = min.x;
        }

        if (min.y < mMin.y) {
            mMin.y = min.y;
        }

        if (min.z < mMin.z) {
            mMin.z = min.z;
        }

        if (mMax.x < max.x) {
            mMax.x = max.x;
        }

        if (mMax.y < max.y) {
            mMax.y = max.y;
        }

        if (mMax.z < max.z) {
            mMax.z = max.z;
        }
    }
}

void CollisionZone::eraseParts(CollisionParts* pParts) {
    CollisionParts** pFound = mParts.begin();
    s32 count = mParts.size();
    CollisionParts** pEnd = mParts.begin() + count;
    while (pFound != pEnd && *pFound != pParts) {
        pFound++;
    }

    if (pFound != pEnd) {
        mParts[pFound - mParts.begin()] = mParts[count - 1];
        mParts.pop_back();
    }
}

Vec3f CollisionParts::getTrans() {
    Vec3f translation;
    mBaseMatrix.getTrans(translation);
    return translation;
}
