#include "Game/Map/CollisionParts.hpp"
#include "Game/Camera/CameraPolygonCodeUtil.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Map/KCollision.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"

CollisionParts::CollisionParts()
    : mFollowMtx(), mHitSensor(), mIsValid(), mIsUpdate(true), mIsUpdateOnce(), mUseEqualScale(), mIsNotUsingScale(), mStillCount(), mBoundingRadius(-1.0f), mScale(1.0f), mKeeperIndex(-1), mZone() {
    mServer = new KCollisionServer();

    mPrevBaseMatrix.identity();
    mBaseMatrix.identity();
    mMatrix.identity();
    NebulaMtxInverse(mBaseMatrix.toMtxPtr(), mInvBaseMatrix.toMtxPtr());
}

void CollisionParts::init(const Pos3f& rMatrix, HitSensor* pHitSensor, const void* pKclData, const void* pMapInfo, s32 keeperIndex,
                          bool /*useAutoScale*/) {
    mServer->init(const_cast< void* >(pKclData), pMapInfo);
    mHitSensor = pHitSensor;

    resetAllMtx(rMatrix);

    Vec3f scale;
    mBaseMatrix.getScale(scale);

    mZone = MR::getCollisionDirector()->getCategoryKeeper(keeperIndex)->getZone(MR::getCurrentPlacementZoneId());

    MR::initCameraCodeCollection(pHitSensor->mHost->mName, mZone->mZoneID);
    mServer->calcFarthestVertexDistance();
    MR::termCameraCodeCollection();

    updateBoundingSphereRange(scale);
    mKeeperIndex = keeperIndex;
}

void CollisionParts::init(const Pos3f& rMatrix, HitSensor* pHitSensor, KCLFile* pModel, const void* pMapInfo, s32 keeperIndex,
                          bool /*useAutoScale*/) {
    mServer->initModel(pModel, pMapInfo);
    mHitSensor = pHitSensor;

    resetAllMtx(rMatrix);

    Vec3f scale;
    mBaseMatrix.getScale(scale);

    mZone = MR::getCollisionDirector()->getCategoryKeeper(keeperIndex)->getZone(MR::getCurrentPlacementZoneId());

    MR::initCameraCodeCollection(pHitSensor->mHost->mName, mZone->mZoneID);
    mServer->calcFarthestVertexDistance();
    MR::termCameraCodeCollection();

    updateBoundingSphereRange(scale);
    mKeeperIndex = keeperIndex;
}

void CollisionParts::addToBelongZone() {
    s32 zoneID = mZone->mZoneID;

    MR::getCollisionDirector()->getCategoryKeeper(mKeeperIndex)->addToZone(this, zoneID);
}

void CollisionParts::removeFromBelongZone() {
    s32 zoneID = mZone->mZoneID;

    MR::getCollisionDirector()->getCategoryKeeper(mKeeperIndex)->removeFromZone(this, zoneID);
}

void CollisionParts::initWithAutoEqualScale(const Pos3f& rMatrix, HitSensor* pHitSensor, const void* pKclData, const void* pMapInfo, s32 keeperIndex,
                                            bool useAutoScale) {
    mUseEqualScale = true;
    mIsNotUsingScale = false;

    init(rMatrix, pHitSensor, pKclData, pMapInfo, keeperIndex, useAutoScale);
}

void CollisionParts::initWithNotUsingScale(const Pos3f& rMatrix, HitSensor* pHitSensor, const void* pKclData, const void* pMapInfo, s32 keeperIndex,
                                           bool useAutoScale) {
    mUseEqualScale = false;
    mIsNotUsingScale = true;

    init(rMatrix, pHitSensor, pKclData, pMapInfo, keeperIndex, useAutoScale);
}

void CollisionParts::resetAllMtx(const Pos3f& rMatrix) {
    bool reset = false;

    if (mIsUpdate || mIsUpdateOnce) {
        reset = true;
    }

    if (!reset) {
        return;
    }

    resetAllMtxPrivate(rMatrix);
}

void CollisionParts::resetAllMtx() {
    bool reset = false;

    if (mIsUpdate || mIsUpdateOnce) {
        reset = true;
    }

    if (reset) {
        Pos3f matrix(mFollowMtx);
        makeEqualScale(matrix);

        resetAllMtxPrivate(matrix);
    }
}

void CollisionParts::forceResetAllMtxAndSetUpdateMtxOneTime() {
    Pos3f matrix(mFollowMtx);
    makeEqualScale(matrix);
    resetAllMtxPrivate(matrix);

    mIsUpdateOnce = true;
}

void CollisionParts::resetAllMtxPrivate(const Pos3f& rMatrix) {
    mPrevBaseMatrix.setInline(rMatrix);
    mBaseMatrix.setInline(rMatrix);
    mMatrix.setInline(rMatrix);
    NebulaMtxInverse(mBaseMatrix.toMtxPtr(), mInvBaseMatrix.toMtxPtr());
}

void CollisionParts::setMtx(const Pos3f& rMatrix) {
    mMatrix.setInline(rMatrix);
}

void CollisionParts::setMtx() {
    mMatrix.setInline(mFollowMtx);
}

void CollisionParts::updateMtx() {
    bool update = false;

    if (mIsUpdate || mIsUpdateOnce) {
        update = true;
    }

    if (!update) {
        if (MR::isSameMtx(mMatrix.toMtxPtr(), mBaseMatrix.toMtxPtr())) {
            mStillCount++;
        }
    } else {
        if (MR::isSameMtx(mMatrix.toMtxPtr(), mBaseMatrix.toMtxPtr())) {
            mStillCount++;
        } else {
            if (mIsUpdateOnce) {
                mStillCount = 1;
            } else {
                mStillCount = 0;
            }

            f32 scale = makeEqualScale(mMatrix);
            f32 scaleChange = scale - mScale;

            if (!MR::isNearZero(scaleChange)) {
                updateBoundingSphereRangePrivate(scale);
            }
        }

        mIsUpdateOnce = false;

        if (mStillCount < 2) {
            mPrevBaseMatrix.setInline(mBaseMatrix);
            mBaseMatrix.setInline(mMatrix);
            NebulaMtxInverse(mBaseMatrix.toMtxPtr(), mInvBaseMatrix.toMtxPtr());
        }
    }
}

f32 CollisionParts::makeEqualScale(Pos3f& mtx) {
    Vec3f scale;
    mtx.getScale(scale);

    Vec3f scaleDiff(scale.x - scale.y, scale.y - scale.z, scale.z - scale.x);

    if (MR::isNearZero(scaleDiff.x) && MR::isNearZero(scaleDiff.y) && MR::isNearZero(scaleDiff.z)) {
        return scale.x;
    }

    f32 uniformScale = 1.0f;
    Vec3f invScale(1.0f, 1.0f, 1.0f);  // the default type keeps the matrix's scale

    if (mIsNotUsingScale) {
        invScale.set< f32 >(uniformScale / scale.x, uniformScale / scale.y, uniformScale / scale.z);
        uniformScale = 1.0f;
    } else if (mUseEqualScale) {
        uniformScale = (scale.x + scale.y + scale.z) / 3.0f;
        invScale.set< f32 >(uniformScale / scale.x, uniformScale / scale.y, uniformScale / scale.z);
    }

    mtx.mMtx[0][0] *= invScale.x;
    mtx.mMtx[1][0] *= invScale.x;
    mtx.mMtx[2][0] *= invScale.x;

    mtx.mMtx[0][1] *= invScale.y;
    mtx.mMtx[1][1] *= invScale.y;
    mtx.mMtx[2][1] *= invScale.y;

    mtx.mMtx[0][2] *= invScale.z;
    mtx.mMtx[1][2] *= invScale.z;
    mtx.mMtx[2][2] *= invScale.z;

    return uniformScale;
}

void CollisionParts::updateBoundingSphereRange() {
    Pos3f matrix(mFollowMtx);
    f32 scale = makeEqualScale(matrix);
    updateBoundingSphereRangePrivate(scale);
}

void CollisionParts::updateBoundingSphereRange(Vec3f scale) {
    f32 range = (scale.x + scale.y + scale.z) / 3.0f;
    updateBoundingSphereRangePrivate(range);
}

void CollisionParts::updateBoundingSphereRangePrivate(f32 scale) {
    mScale = scale;
    mBoundingRadius = scale * mServer->mMaxVertexDistance;
}

const char* CollisionParts::getHostName() const {
    if (mHitSensor == nullptr) {
        return nullptr;
    }

    LiveActor* actor = mHitSensor->mHost;

    if (actor == nullptr) {
        return nullptr;
    }

    return actor->mName;
}

s32 CollisionParts::getPlacementZoneID() const {
    return mZone->mZoneID;
}

bool CollisionParts::checkStrikePoint(HitInfo* pHitInfo, const Vec3f& rPos) {
    Vec3f localPos;
    mInvBaseMatrix.mult(rPos, localPos);
    Vec3f scale;
    mInvBaseMatrix.getScale(scale);
    f32 averageScale = (scale.x + scale.y + scale.z) / 3.0f;
    f32 distance;
    KC_PrismData* prism = nullptr;
    u8 classification;

    if (1.0f < averageScale) {
        f32 radius = 20.0f * averageScale;
        mServer->checkSphereWithThickness(&localPos, radius, averageScale, 1, &prism, &distance, &classification, 2.0f * radius);
        if (prism == nullptr) {
            return false;
        }

        Vec3f offset(localPos);
        offset -= mServer->getPos(prism, 0);
        Vec3f normal(*mServer->getFaceNormal(prism));
        distance = -offset.x * normal.x - offset.y * normal.y - offset.z * normal.z;
    } else {
        prism = mServer->checkPoint(&localPos, averageScale, &distance);
        if (prism == nullptr) {
            return false;
        }
    }

    if (pHitInfo != nullptr) {
        pHitInfo->mParentTriangle.fillData(this, mServer->toIndex(prism), mHitSensor);
        f32 worldDistance = distance / averageScale;
        pHitInfo->mDepth = worldDistance;
        pHitInfo->mHitPos = rPos + *pHitInfo->mParentTriangle.getNormal(0) * worldDistance;
    }

    return true;
}

namespace {
    // A query's working lists: room for the common case on the stack, more when more hits were asked for.
    struct StrikeLists {
        explicit StrikeLists(u32 maxHits) {
            const bool fits = maxHits <= ARRAY_SIZEU(mPrismRoom);
            mPrisms = fits ? mPrismRoom : new KC_PrismData*[maxHits];
            mDistances = fits ? mDistanceRoom : new f32[maxHits];
            mClassifications = fits ? mClassificationRoom : new u8[maxHits];
        }

        ~StrikeLists() {
            if (mPrisms != mPrismRoom) {
                delete[] mPrisms;
                delete[] mDistances;
                delete[] mClassifications;
            }
        }

        KC_PrismData* mPrismRoom[64];
        f32 mDistanceRoom[64];
        u8 mClassificationRoom[64];
        KC_PrismData** mPrisms;
        f32* mDistances;
        u8* mClassifications;
    };
}  // namespace

u32 CollisionParts::checkStrikeBall(HitInfo* pHitInfo, u32 maxHits, const Vec3f& rPos, f32 radius, bool checkMovement,
                                    const TriangleFilterBase* pFilter) {
    StrikeLists lists(maxHits);
    KC_PrismData** const prisms = lists.mPrisms;
    f32* const distances = lists.mDistances;
    u8* const classifications = lists.mClassifications;
    Pos3f inversePrev;
    Vec3f localPos;
    mInvBaseMatrix.mult(rPos, localPos);
    Vec3f scale;
    mInvBaseMatrix.getScale(scale);
    f32 averageScale = (scale.x + scale.y + scale.z) / 3.0f;
    f32 inverseScale = 1.0f / averageScale;
    radius *= averageScale;
    Vec3f movement(0, 0, 0);

    if (checkMovement && mStillCount == 0) {
        NebulaMtxInverse(mPrevBaseMatrix.toMtxPtr(), inversePrev.toMtxPtr());
        Vec3f prevPos;
        inversePrev.mult(rPos, prevPos);
        Vec3f localMovement(localPos - prevPos);
        Vec3f worldMovement(localMovement);
        mBaseMatrix.mult33(worldMovement, worldMovement);
        // One check per 35 units the part moved; 35000 or more, or not a number, takes 1000 (it would not end).
        const f32 movedLength = localMovement.length();
        s32 steps = movedLength < 35000.0f ? static_cast< s32 >((1.0f / 35.0f) * movedLength) + 1 : 1000;
        Vec3f step(localMovement);

        if (steps > 1) {
            step /= steps;
        }

        Vec3f offset;
        offset.zero();

        for (s32 i = 0; i <= steps; i++) {
            movement.set(-(localMovement - offset));
            mBaseMatrix.mult33(movement, movement);
            const Vec3f* direction = i == steps ? nullptr : &worldMovement;

            u32 count = checkStrikeBallCore(pHitInfo, maxHits, prevPos + offset, movement, radius, averageScale, inverseScale, prisms, distances,
                                            classifications, pFilter, direction);
            if (count != 0) {
                return count;
            }

            offset += step;
        }

        return 0;
    } else {
        return checkStrikeBallCore(pHitInfo, maxHits, localPos, Vec3f(0, 0, 0), radius, averageScale, inverseScale, prisms, distances,
                                   classifications, pFilter, nullptr);
    }
}

u32 CollisionParts::checkStrikeBallCore(HitInfo* pHitInfo, u32 maxHits, const Vec3f& rPos, const Vec3f& rMovement, f32 radius, f32 scale,
                                        f32 inverseScale, KC_PrismData** pPrisms, f32* pDistances, u8* pClassifications,
                                        const TriangleFilterBase* pFilter, const Vec3f* pDirection) {
    const Vec3f* normal;
    u32 count =
        mServer->checkSphere(&rPos, radius, scale, maxHits, pPrisms, pDistances, pClassifications);
    u32 hitCount = 0;

    for (u32 i = 0; i < count; i++) {
        HitInfo& hit = pHitInfo[hitCount];
        Vec3f position(rPos);
        calcCollidePosition(&position, *pPrisms[i], pClassifications[i]);
        mBaseMatrix.mult(position, hit.mHitPos);
        hit.mParentTriangle.fillData(this, mServer->toIndex(pPrisms[i]), mHitSensor);

        if (pFilter != nullptr && pFilter->isInvalidTriangle(&hit.mParentTriangle)) {
            continue;
        }

        if (pDirection != nullptr && 0.0f < pDirection->dot(*hit.mParentTriangle.getFaceNormal())) {
            continue;
        }

        hit.mDepth = inverseScale * pDistances[i];
        hit.mHitPart = pClassifications[i];
        hit.mNormalMove.set(rMovement);
        normal = hit.mParentTriangle.getFaceNormal();
        hit.mNormalMove.scale(normal->dot(hit.mNormalMove), *normal);
        hitCount++;
    }

    return hitCount;
}

u32 CollisionParts::checkStrikeBallWithThickness(HitInfo* pHitInfo, u32 maxHits, const Vec3f& rPos, f32 radius, f32 thickness,
                                                 const TriangleFilterBase* pFilter) {
    StrikeLists lists(maxHits);
    KC_PrismData** const prisms = lists.mPrisms;
    f32* const distances = lists.mDistances;
    u8* const classifications = lists.mClassifications;
    Vec3f localPos;
    mInvBaseMatrix.mult(rPos, localPos);
    Vec3f scale;
    mInvBaseMatrix.getScale(scale);
    f32 averageScale = (scale.x + scale.y + scale.z) / 3.0f;
    f32 scaledRadius = radius * averageScale;
    u32 count = mServer->checkSphereWithThickness(&localPos,scaledRadius, averageScale, maxHits, prisms, distances, classifications, thickness);
    f32 inverseScale = 1.0f / averageScale;
    u32 hitCount = 0;

    for (u32 i = 0; i < count; i++) {
        HitInfo& hit = pHitInfo[hitCount];
        Vec3f position(localPos);
        calcCollidePosition(&position, *prisms[i], classifications[i]);
        mBaseMatrix.mult(position, hit.mHitPos);
        hit.mParentTriangle.fillData(this, mServer->toIndex(prisms[i]), mHitSensor);

        if (pFilter != nullptr && pFilter->isInvalidTriangle(&hit.mParentTriangle)) {
            continue;
        }

        hit.mDepth = inverseScale * distances[i];
        hit.mHitPart = classifications[i];
        hitCount++;
    }

    return hitCount;
}

void CollisionParts::calcCollidePosition(Vec3f* pPos, const KC_PrismData& rPrism, u8 classification) {
    Vec3f offset;
    Vec3f normal;

    switch (classification) {
    // The face: into the triangle's plane.
    case 1:
        projectToPlane(pPos, *pPos, mServer->getPos(&rPrism, 0), *mServer->getNormal(rPrism.mNormalIndex));
        break;
    // Edge plane 0 (vertices 0-2), 1 (0-1) or 2 (1-2): into the triangle's plane, then onto the edge.
    case 2:
    case 3:
    case 4: {
        s32 plane = classification - 2;
        projectToPlane(pPos, *pPos, mServer->getPos(&rPrism, 0), *mServer->getNormal(rPrism.mNormalIndex));
        normal.set(*mServer->getNormal(rPrism.mEdgeIndices[plane]));
        offset.set(*pPos);
        offset -= mServer->getPos(&rPrism, plane == 2 ? 1 : 0);
        *pPos += -normal * offset.dot(normal);
        break;
    }
    // Vertex 0-2.
    case 5:
    case 6:
    case 7:
        pPos->set(mServer->getPos(&rPrism, classification - 5));
        break;
    }
}

void CollisionParts::projectToPlane(Vec3f* pProjected, const Vec3f& rPos, const Vec3f& rOrigin, const Vec3f& rNormal) {
    Vec3f projected = rPos;

    f32 distance = (rPos - rOrigin).dot(rNormal);

    projected.add(-rNormal * distance);
    pProjected->set(projected);
}

u32 CollisionParts::checkStrikeLine(HitInfo* pHitInfo, u32 maxHits, const Vec3f& rPos, const Vec3f& rDirection, const TriangleFilterBase* pFilter) {
    StrikeLists lists(maxHits);
    KC_PrismData** const prisms = lists.mPrisms;
    f32* const distances = lists.mDistances;
    u8* const classifications = lists.mClassifications;
    f32 length = rDirection.length();
    Vec3f localPos;
    Vec3f localDirection;
    mInvBaseMatrix.mult(rPos, localPos);
    mInvBaseMatrix.mult(rPos + rDirection, localDirection);
    localDirection = localDirection - localPos;
    u32 count = 0;
    mServer->checkArrow(localPos, localDirection, distances, classifications, &count, prisms, maxHits);
    u32 hitCount = 0;

    for (u32 i = 0; i < count; i++) {
        HitInfo& hit = pHitInfo[hitCount];
        Vec3f position(localPos + localDirection * distances[i]);
        mBaseMatrix.mult(position, position);
        hit.mParentTriangle.fillData(this, mServer->toIndex(prisms[i]), mHitSensor);

        if (pFilter != nullptr && pFilter->isInvalidTriangle(&hit.mParentTriangle)) {
            continue;
        }

        hit.mDepth = length * distances[i];
        hit.mHitPos = position;
        hit.mHitPart = classifications[i];
        hitCount++;
    }

    return hitCount;
}

namespace {
    // Area lists are asked for on the game thread. The list lives in the permanent arena and grows to the largest request.
    KC_PrismData** sAreaPrisms = nullptr;
    u32 sAreaPrismCapacity = 0;

    KC_PrismData** areaPrisms(u32 maxTriangles) {
        if (maxTriangles > sAreaPrismCapacity) {
            delete[] sAreaPrisms;
            sAreaPrisms = new (Arenas::permanent(), 0) KC_PrismData*[maxTriangles];
            sAreaPrismCapacity = maxTriangles;
        }
        return sAreaPrisms;
    }
}  // namespace

u32 CollisionParts::createAreaPolygonList(Triangle* pTriangles, u32 maxTriangles, const Vec3f& rMin, const Vec3f& rMax) {
    KC_PrismData** const prisms = ::areaPrisms(maxTriangles);
    Pos3f rotation;
    NebulaMtxCopy(mInvBaseMatrix.toMtxPtr(), rotation.toMtxPtr());
    rotation.zeroTrans();
    Vec3f min;
    Vec3f max;
    mInvBaseMatrix.mult(rMin, min);
    mInvBaseMatrix.mult(rMax, max);
    u32 count = mServer->checkArea3D(&min, &max, prisms, maxTriangles);
    if (count == 0) {
        return 0;
    }

    for (u32 i = 0; i < count; i++) {
        pTriangles[i].fillData(this, mServer->toIndex(prisms[i]), mHitSensor);
    }

    return count;
}

u32 CollisionParts::createAreaPolygonListArray(Triangle* pTriangles, u32 maxTriangles, Vec3f* pPoints, u32 numPoints) {
    if (numPoints == 0) {
        return 0;
    }
    KC_PrismData** const prisms = ::areaPrisms(maxTriangles);
    Pos3f rotation;
    NebulaMtxCopy(mInvBaseMatrix.toMtxPtr(), rotation.toMtxPtr());
    rotation.zeroTrans();

    // The points' box in the part's space.
    Vec3f min;
    mInvBaseMatrix.mult(pPoints[0], min);
    Vec3f max = min;
    for (u32 i = 1; i < numPoints; i++) {
        Vec3f local;
        mInvBaseMatrix.mult(pPoints[i], local);
        min.x = local.x < min.x ? local.x : min.x;
        min.y = local.y < min.y ? local.y : min.y;
        min.z = local.z < min.z ? local.z : min.z;
        max.x = local.x > max.x ? local.x : max.x;
        max.y = local.y > max.y ? local.y : max.y;
        max.z = local.z > max.z ? local.z : max.z;
    }
    u32 count = mServer->checkArea3D(&min, &max, prisms, maxTriangles);
    if (count == 0) {
        return 0;
    }

    for (u32 i = 0; i < count; i++) {
        pTriangles[i].fillData(this, mServer->toIndex(prisms[i]), mHitSensor);
    }

    return count;
}

void CollisionParts::calcForceMovePower(Vec3f* pMovement, const Vec3f& rPos) const {
    Vec3f position(rPos);
    Mtx34f inversePrev;
    NebulaMtxInverse(mPrevBaseMatrix, inversePrev);

    inversePrev.mult(position, position);
    mBaseMatrix.mult(position, position);

    position.sub(rPos);
    *pMovement = position;
}
