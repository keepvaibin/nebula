#include "Game/Util/MapUtil.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/CollisionPartsFilter.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"

namespace {
    // getNearPolyOnLineSort's hits, nearest first, in the permanent arena; it grows to the most hits one line has had.
    HitInfo* sSortBuffer = nullptr;
    f32* sSortDistance = nullptr;
    u32 sSortRoom = 0;
    u32 sSortCount = 0;
}  // namespace

namespace {
    u32 getStrikeInfoNumCategory(s32 category);
    bool getFirstPolyOnLineCategoryExceptActor(Vec3f*, Triangle*, const Vec3f&, const Vec3f&, const LiveActor*, s32);

    u32 getStrikeInfoNumCategory(s32 category) {
        return MR::getCollisionDirector()->getCategoryKeeper(category)->mNumHits;
    }

    const HitInfo* getStrikeInfoCategory(s32 category, u32 index) {
        return MR::getCollisionDirector()->getCategoryKeeper(category)->getStrikeInfo(index);
    }

    bool getFirstPolyOnLineCategory(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                    const TriangleFilterBase* pTriangleFilter, const CollisionPartsFilterBase* pPartsFilter, s32 category) {
        u32 count = MR::getCollisionDirector()->getCategoryKeeper(category)->checkStrikeLine(rStart, rDirection, 0, pPartsFilter, nullptr);
        const HitInfo* pHit;
        if (count == 0) {
            return false;
        }

        f32 minDistance = 1000000.0f;
        s32 nearest = -1;
        for (u32 i = 0; i < count; i++) {
            pHit = getStrikeInfoCategory(category, i);
            if (pTriangleFilter && pTriangleFilter->isInvalidTriangle(&pHit->mParentTriangle)) {
                continue;
            }

            if (minDistance > pHit->mDepth) {
                nearest = i;
                minDistance = pHit->mDepth;
            }
        }

        if (nearest == -1) {
            return false;
        }

        pHit = getStrikeInfoCategory(category, nearest);
        if (pPos) {
            *pPos = pHit->mHitPos;
        }

        if (pTriangle) {
            *pTriangle = pHit->mParentTriangle;
        }

        return true;
    }

    bool getFirstPolyOnLineCategoryExceptSensor(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                                const HitSensor* pSensor, s32 category) {
        CollisionPartsFilterSensor filter(pSensor);
        return getFirstPolyOnLineCategory(pPos, pTriangle, rStart, rDirection, nullptr, &filter, category);
    }

    bool getFirstPolyOnLineCategoryExceptActor(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                               const LiveActor* pActor, s32 category) {
        CollisionPartsFilterActor filter(pActor);
        return getFirstPolyOnLineCategory(pPos, pTriangle, rStart, rDirection, nullptr, &filter, category);
    }
};  // namespace

namespace MR {
    const Vec3f* getNormal(const Triangle* pTriangle) {
        return pTriangle->getNormal(0);
    }

    bool isWallPolygon(const Vec3f& rNormal, const Vec3f& rGravity) {
        if (isNearZero(rNormal)) {
            return false;
        }

        return isWallPolygon(rNormal.dot(rGravity));
    }

    bool isFloorPolygon(const Vec3f& rNormal, const Vec3f& rGravity) {
        if (isNearZero(rNormal)) {
            return false;
        }

        return isFloorPolygon(rNormal.dot(rGravity));
    }

    // A floor no steeper than acos(minCos).
    bool isFloorPolygonCos(const Vec3f& rNormal, const Vec3f& rGravity, f32 minCos) {
        if (isNearZero(rNormal)) {
            return false;
        }

        if (-rNormal.dot(rGravity) < minCos) {
            return false;
        }

        return isFloorPolygon(rNormal.dot(rGravity));
    }

    // normalDotGravity: the polygon's normal dotted with gravity. Within 70 degrees of upright is a wall.
    bool isWallPolygon(f32 normalDotGravity) {
        return MR::abs(normalDotGravity) < 0.34202015f;  // cos 70
    }

    bool isFloorPolygon(f32 normalDotGravity) {
        if (isWallPolygon(normalDotGravity)) {
            return false;
        }

        return normalDotGravity < 0.0f;
    }

    bool isCeilingPolygon(f32 normalDotGravity) {
        if (isWallPolygon(normalDotGravity) || isFloorPolygon(normalDotGravity)) {
            return false;
        }

        return true;
    }

    // The floor codes name no Water or Shallow (Shallow is a sound code), so no polygon is water by its floor code.
    bool isWaterPolygon(const Triangle*) {
        return false;
    }

    bool isThroughPolygon(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == static_cast< s32 >(CollisionFloorCode_PullBack);
    }

    bool getFirstPolyOnLineToMap(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection) {
        return ::getFirstPolyOnLineCategory(pPos, pTriangle, rStart, rDirection, nullptr, nullptr, 0);
    }

    bool getFirstPolyOnLineToMapAndMoveLimit(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection) {
        Triangle mapTriangle;
        Vec3f mapPos;
        bool hitMap = ::getFirstPolyOnLineCategory(&mapPos, &mapTriangle, rStart, rDirection, nullptr, nullptr, 0);
        Triangle limitTriangle;
        Vec3f limitPos;
        bool hitLimit = ::getFirstPolyOnLineCategory(&limitPos, &limitTriangle, rStart, rDirection, nullptr, nullptr, 3);

        if (hitMap && hitLimit) {
            if (rStart.distance(mapPos) >= rStart.distance(limitPos)) {
                hitMap = false;
            }
        }

        if (hitMap) {
            if (pPos) {
                pPos->set(mapPos);
            }

            if (pTriangle) {
                *pTriangle = mapTriangle;
            }

            return true;
        }

        if (hitLimit) {
            if (pPos) {
                pPos->set(limitPos);
            }

            if (pTriangle) {
                *pTriangle = limitTriangle;
            }

            return true;
        }

        return false;
    }

    bool getFirstPolyOnLineToWaterSurface(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection) {
        return ::getFirstPolyOnLineCategory(pPos, pTriangle, rStart, rDirection, nullptr, nullptr, 2);
    }

    bool getFirstPolyOnLineToMapExceptSensor(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                             const HitSensor* pSensor) {
        return ::getFirstPolyOnLineCategoryExceptSensor(pPos, pTriangle, rStart, rDirection, pSensor, 0);
    }

    bool getFirstPolyOnLineToMapExceptActor(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                            const LiveActor* pActor) {
        return ::getFirstPolyOnLineCategoryExceptActor(pPos, pTriangle, rStart, rDirection, pActor, 0);
    }

    bool getFirstPolyOnLineToMap(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                 const CollisionPartsFilterBase* pPartsFilter, const TriangleFilterBase* pTriangleFilter) {
        return ::getFirstPolyOnLineCategory(pPos, pTriangle, rStart, rDirection, pTriangleFilter, pPartsFilter, 0);
    }

    bool getFirstPolyOnLineToWaterSurface(Vec3f* pPos, Triangle* pTriangle, const Vec3f& rStart, const Vec3f& rDirection,
                                          const CollisionPartsFilterBase* pPartsFilter, const TriangleFilterBase* pTriangleFilter) {
        return ::getFirstPolyOnLineCategory(pPos, pTriangle, rStart, rDirection, pTriangleFilter, pPartsFilter, 2);
    }

    bool getFirstPolyNormalOnLineToMap(Vec3f* pNormal, const Vec3f& rStart, const Vec3f& rDirection, Vec3f* pPos, const HitSensor* pSensor) {
        Triangle triangle;
        if (!::getFirstPolyOnLineCategoryExceptSensor(pPos, &triangle, rStart, rDirection, pSensor, 0)) {
            return false;
        }

        pNormal->set(*triangle.getFaceNormal());
        return true;
    }

    u32 getNearPolyOnLineSort(const Vec3f& rOrigin, const Vec3f& rStart, const Vec3f& rDirection, const HitSensor* pSensor) {
        CollisionCategorizedKeeper* pKeeper = getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map);
        const u32 count = pKeeper->checkStrikeLine(rStart, rDirection, 0, nullptr, nullptr);
        sSortCount = 0;
        if (count > sSortRoom) {
            delete[] sSortBuffer;
            delete[] sSortDistance;
            sSortRoom = count * 2;
            sSortBuffer = new (Arenas::permanent(), 0) HitInfo[sSortRoom];
            sSortDistance = new (Arenas::permanent(), 0) f32[sSortRoom];
        }

        // Insertion by distance: equal distances keep the order the keeper found them in.
        for (u32 i = 0; i < count; i++) {
            const HitInfo* pHit = pKeeper->getStrikeInfo(i);
            if (pSensor != nullptr && pHit->mParentTriangle.getSensor() == pSensor) {
                continue;
            }

            Vec3f offset(rOrigin);
            offset.sub(pHit->mHitPos);
            const f32 distance = offset.length();
            u32 at = sSortCount;
            for (; at > 0 && sSortDistance[at - 1] > distance; at--) {
                sSortBuffer[at] = sSortBuffer[at - 1];
                sSortDistance[at] = sSortDistance[at - 1];
            }
            sSortBuffer[at] = *pHit;
            sSortDistance[at] = distance;
            sSortCount++;
        }

        return sSortCount;
    }

    bool getSortedPoly(Vec3f* pDst, Triangle* pTriangle, u32 sortIndex) {
        if (sSortCount <= sortIndex) {
            return false;
        }

        const HitInfo& rHit = sSortBuffer[sortIndex];
        if (pTriangle != nullptr) {
            *pTriangle = rHit.mParentTriangle;
        }

        if (pDst != nullptr) {
            *pDst = rHit.mHitPos;
        }

        return true;
    }

    bool isExistMapCollision(const Vec3f& rStart, const Vec3f& rDirection) {
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeLine(rStart, rDirection, 1, nullptr, nullptr) != 0;
    }

    bool isExistMoveLimitCollision(const Vec3f& rStart, const Vec3f& rDirection) {
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_MoveLimit)->checkStrikeLine(rStart, rDirection, 1, nullptr, nullptr) != 0;
    }

    bool isExistMapCollisionExceptActor(const Vec3f& rStart, const Vec3f& rDirection, const LiveActor* pActor) {
        CollisionPartsFilterActor filter(pActor);
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeLine(rStart, rDirection, 1, &filter, nullptr) != 0;
    }

    bool checkStrikePointToMap(const Vec3f& rPosition, HitInfo* pHitInfo) {
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikePoint(rPosition, pHitInfo) != 0;
    }

    bool checkStrikeBallToMap(const Vec3f& rPosition, f32 ballRadius) {
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeBall(rPosition, ballRadius, false, nullptr, nullptr) != 0;
    }

    bool calcMapGround(const Vec3f& rPos, Vec3f* pGround, f32 length) {
        Vec3f direction(0.0f, -length, 0.0f);
        return ::getFirstPolyOnLineCategory(pGround, nullptr, rPos, direction, nullptr, nullptr, 0);
    }

    bool calcMapGroundUpper(Vec3f* pGround, const LiveActor* pActor) {
        CollisionParts* pParts = getCollisionParts(pActor);
        Vec3f gravity;
        calcGravityVector(pActor, pActor->mPosition, &gravity, nullptr, 0);
        f32 radius = 0.0f;
        calcModelBoundingRadius(&radius, pActor);
        f32 upperDistance = 2.5f * radius;
        f32 length = 4.0f * radius;
        Vec3f start(pActor->mPosition - gravity * upperDistance);
        Vec3f direction(gravity * length);
        HitInfo hit;
        pParts->checkStrikeLine(&hit, 1, start, direction, nullptr);
        CollisionPartsFilterSensor filter(pParts->mHitSensor);
        return ::getFirstPolyOnLineCategory(pGround, nullptr, hit.mHitPos, direction, nullptr, &filter, 0);
    }

    bool isFallNextMove(const LiveActor* pActor, f32 forwardDistance, f32 upperDistance, f32 lowerDistance, const TriangleFilterBase* pTriangleFilter) {
        return isFallNextMove(pActor->mPosition, pActor->mVelocity, pActor->mGravity, forwardDistance, upperDistance, lowerDistance, pTriangleFilter);
    }

    bool isFallNextMove(const Vec3f& rPos, const Vec3f& rVelocity, const Vec3f& rGravity, f32 forwardDistance, f32 upperDistance,
                        f32 lowerDistance, const TriangleFilterBase* pTriangleFilter) {
        if (isNearZero(rGravity)) {
            return false;
        }

        Vec3f forward;
        forward.scaleAdd(-rGravity.dot(rVelocity), rGravity, rVelocity);
        if (isNearZero(forward)) {
            return false;
        }

        normalize(&forward);
        forward *= forwardDistance;
        Vec3f start;
        start = forward + rPos - rGravity * upperDistance;
        bool hit = getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeLine(start, rGravity * (upperDistance + lowerDistance), 1, nullptr,
                                                                                 pTriangleFilter) != 0;
        return !hit;
    }

    bool isFallOrDangerNextMove(const LiveActor* pActor, f32 forwardDistance, f32 upperDistance, f32 lowerDistance) {
        TriangleFilterDangerCode filter;
        return isFallNextMove(pActor, forwardDistance, upperDistance, lowerDistance, &filter);
    }

    bool isFallOrDangerNextMove(const Vec3f& rPos, const Vec3f& rVelocity, const Vec3f& rGravity, f32 forwardDistance, f32 upperDistance,
                                f32 lowerDistance) {
        TriangleFilterDangerCode filter;
        return isFallNextMove(rPos, rVelocity, rGravity, forwardDistance, upperDistance, lowerDistance, &filter);
    }

    void calcVelocityMovingPoint(const Triangle* pTriangle, const Vec3f& rPoint, Vec3f* pVelocity) {
        if (isSameMtx(pTriangle->getBaseMtx()->toMtxPtr(), pTriangle->getPrevBaseMtx()->toMtxPtr())) {
            pVelocity->zero();
            return;
        }

        Vec3f localPoint;
        NebulaMtxMultVec(pTriangle->getBaseInvMtx()->toMtxPtr(), rPoint, &localPoint);
        Vec3f previousPoint;
        NebulaMtxMultVec(pTriangle->getPrevBaseMtx()->toMtxPtr(), &localPoint, &previousPoint);
        *pVelocity = rPoint - previousPoint;
    }

    u32 createAreaPolygonList(Triangle* pTriangles, u32 maxTriangles, const Vec3f& rStart, const Vec3f& rEnd) {
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->createAreaPolygonList(pTriangles, maxTriangles, rStart, rEnd);
    }

    u32 createAreaPolygonListArray(Triangle* pTriangles, u32 maxTriangles, Vec3f* pVertices, u32 vertexCount) {
        return getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->createAreaPolygonListArray(pTriangles, maxTriangles, pVertices, vertexCount);
    }

    bool trySetMoveLimitCollision(LiveActor* pActor) {
        Vec3f start(pActor->mPosition);
        Vec3f direction(pActor->mGravity);
        start -= direction * 150.0f;
        direction *= 1000.0f;
        if (getCollisionDirector()->getCategoryKeeper(CollisionCategory_MoveLimit)->checkStrikeLine(start, direction, 0, nullptr, nullptr)) {
            pActor->mBinder->setExCollisionParts(getCollisionDirector()->getCategoryKeeper(CollisionCategory_MoveLimit)->getStrikeInfo(0)->mParentTriangle.mParts);
            return true;
        }

        if (getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeLine(start, direction, 0, nullptr, nullptr)) {
            const HitInfo* pHit = getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->getStrikeInfo(0);
            CollisionParts* pSameParts = nullptr;
            CollisionParts* pParts = pHit->mParentTriangle.mParts;
            getCollisionDirector()->getCategoryKeeper(CollisionCategory_MoveLimit)->searchSameHostParts(&pSameParts, pParts);
            pActor->mBinder->setExCollisionParts(pSameParts);
            return true;
        }

        return false;
    }

    bool isBindedGroundIce(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeIce(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundSand(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeSand(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundDamageFire(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeDamageFire(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundWaterBottomH(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeWaterBottomH(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundWaterBottomM(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeWaterBottomM(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundWater(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeWaterIter(pActor->mBinder->mGroundInfo.mParentTriangle.getAttributes());
    }

    bool isBindedGroundSinkDeath(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeSinkDeath(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundAreaMove(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeAreaMove(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundRailMove(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeRailMove(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedGroundBrake(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (!pActor->mBinder->isBindedGround()) {
            return false;
        }

        return isGroundCodeBrake(&pActor->mBinder->mGroundInfo.mParentTriangle);
    }

    bool isBindedDamageFire(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (pActor->mBinder->isBindedGround() && isGroundCodeDamageFire(&pActor->mBinder->mGroundInfo.mParentTriangle)) {
            return true;
        }

        if (pActor->mBinder->isBindedWall() && isGroundCodeDamageFire(&pActor->mBinder->mWallInfo.mParentTriangle)) {
            return true;
        }

        if (pActor->mBinder->isBindedRoof() && isGroundCodeDamageFire(&pActor->mBinder->mRoofInfo.mParentTriangle)) {
            return true;
        }

        return false;
    }

    bool isBindedDamageElectric(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        if (pActor->mBinder->isBindedGround() && isGroundCodeDamageElectric(&pActor->mBinder->mGroundInfo.mParentTriangle)) {
            return true;
        }

        if (pActor->mBinder->isBindedWall() && isGroundCodeDamageElectric(&pActor->mBinder->mWallInfo.mParentTriangle)) {
            return true;
        }

        if (pActor->mBinder->isBindedRoof() && isGroundCodeDamageElectric(&pActor->mBinder->mRoofInfo.mParentTriangle)) {
            return true;
        }

        return false;
    }

    u32 getCameraID(const Triangle* pTriangle) {
        return getCollisionDirector()->mCode->getCameraID(*pTriangle);
    }

    s32 getFloorCodeIndex(const TableIter& rIter) {
        return getCollisionDirector()->mCode->getFloorCode(rIter);
    }

    s32 getSoundCodeIndex(const TableIter& rIter) {
        return getCollisionDirector()->mCode->getSoundCode(rIter);
    }

    s32 getFloorCodeIndex(const Triangle* pTriangle) {
        return getCollisionDirector()->mCode->getFloorCode(pTriangle->getAttributes());
    }

    s32 getWallCodeIndex(const Triangle* pTriangle) {
        return getCollisionDirector()->mCode->getWallCode(pTriangle->getAttributes());
    }

    s32 getSoundCodeIndex(const Triangle* pTriangle) {
        return getCollisionDirector()->mCode->getSoundCode(pTriangle->getAttributes());
    }

    s32 getCameraCodeIndex(const Triangle* pTriangle) {
        return getCollisionDirector()->mCode->getCameraCode(pTriangle->getAttributes());
    }

    bool isGroundCodeWaterIter(const TableIter& rIter) {
        s32 code = getFloorCodeIndex(rIter);

        return code == CollisionFloorCode_WaterBottomH || code == CollisionFloorCode_WaterBottomM || code == CollisionFloorCode_WaterBottomL ||
               code == CollisionFloorCode_Wet;
    }

    bool isGroundCodeDeath(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_Death;
    }

    bool isGroundCodeDamage(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_DamageNormal;
    }

    bool isGroundCodeIce(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_Ice;
    }

    bool isGroundCodeDamageFire(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_DamageFire;
    }

    bool isGroundCodeFireDance(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_FireDance;
    }

    bool isGroundCodeSand(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_Sand;
    }

    bool isGroundCodeDamageElectric(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_DamageElectric;
    }

    bool isGroundCodeWaterBottomH(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_WaterBottomH;
    }

    bool isGroundCodeWaterBottomM(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_WaterBottomM;
    }

    bool isGroundCodeSinkDeath(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_SinkDeath;
    }

    bool isGroundCodeRailMove(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_RailMove;
    }

    bool isGroundCodeAreaMove(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_AreaMove;
    }

    bool isGroundCodeNoStampSand(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_NoStampSand;
    }

    bool isGroundCodeSinkDeathMud(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_SinkDeathMud;
    }

    bool isGroundCodeBrake(const Triangle* pTriangle) {
        return getFloorCodeIndex(pTriangle) == CollisionFloorCode_Brake;
    }

    bool isWallCodeGhostThrough(const Triangle* pTriangle) {
        return getWallCodeIndex(pTriangle) == CollisionWallCode_GhostThroughCode;
    }

    bool isWallCodeRebound(const Triangle* pTriangle) {
        return getWallCodeIndex(pTriangle) == CollisionWallCode_Rebound;
    }

    bool isWallCodeNoAction(const Triangle* pTriangle) {
        return getWallCodeIndex(pTriangle) == CollisionWallCode_NoAction;
    }

    bool isCameraCodeThrough(const Triangle* pTriangle) {
        return getCameraCodeIndex(pTriangle) == CollisionCameraCode_Through;
    }

    bool isCodeSand(const Triangle* pTriangle) {
        return ((getSoundCodeIndex(pTriangle) == CollisionSoundCode_Sand || getSoundCodeIndex(pTriangle) == CollisionSoundCode_Beach) ||
                isGroundCodeSand(pTriangle)) ||
               isGroundCodeNoStampSand(pTriangle);
    }

    bool getFirstPolyOnLineBFast(const Vec3f& rStart, const Vec3f& rDirection, Vec3f* pPos, Triangle* pTriangle) {
        Triangle triangle;
        f32 remaining = rDirection.length();
        if (!(remaining < 1.0e9f)) {
            return false;
        }

        Vec3f direction(rDirection);
        direction.normalize();
        Vec3f start(rStart);
        Vec3f step(direction * 5000.0f);
        do {
            f32 length = 5000.0f;
            if (remaining < length) {
                length = remaining;
                step = direction * remaining;
            }

            u32 count = getNearPolyOnLineSort(start, start, step, nullptr);
            for (u32 i = 0; i < count; i++) {
                Triangle hitTriangle;
                Vec3f hitPos;
                if (getSortedPoly(&hitPos, &hitTriangle, i)) {
                    if (isWaterPolygon(&hitTriangle)) {
                        continue;
                    }

                    Vec3f backPos(hitPos - *getNormal(&hitTriangle) * 5.0f);
                    Vec3f normal(*getNormal(&hitTriangle) * 35.0f);
                    if (isExistMapCollision(backPos, normal)) {
                        continue;
                    }
                }

                if (pPos) {
                    *pPos = hitPos;
                }

                if (pTriangle) {
                    *pTriangle = hitTriangle;
                }

                return true;
            }

            remaining -= length;
            start += step;
        } while (!isNearZero(remaining));

        return false;
    }
};  // namespace MR

namespace Collision {
    s32 checkStrikePointToMap(const Vec3f& rPoint, HitInfo* pHit) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikePoint(rPoint, pHit);
    }

    s32 checkStrikeBallToMap(const Vec3f& rCenter, f32 radius, const CollisionPartsFilterBase* pPartsFilter,
                             const TriangleFilterBase* pTriangleFilter) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeBall(rCenter, radius, false, pPartsFilter, pTriangleFilter);
    }

    s32 checkStrikeBallToMapWithMovingReaction(const Vec3f& rCenter, f32 radius, const CollisionPartsFilterBase* pPartsFilter,
                                               const TriangleFilterBase* pTriangleFilter) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeBall(rCenter, radius, true, pPartsFilter, pTriangleFilter);
    }

    s32 checkStrikeBallToMapWithThickness(const Vec3f& rCenter, f32 radius, f32 thickness, const CollisionPartsFilterBase* pPartsFilter,
                                          const TriangleFilterBase* pTriangleFilter) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeBallWithThickness(rCenter, radius, thickness, pPartsFilter,
                                                                                              pTriangleFilter);
    }

    s32 checkStrikeLineToMap(const Vec3f& rStart, const Vec3f& rDirection, s32 count, const CollisionPartsFilterBase* pPartsFilter,
                             const TriangleFilterBase* pTriangleFilter) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->checkStrikeLine(rStart, rDirection, count, pPartsFilter, pTriangleFilter);
    }

    s32 checkStrikeLineToSunshade(const Vec3f& rStart, const Vec3f& rDirection, s32 count, const CollisionPartsFilterBase* pPartsFilter,
                                  const TriangleFilterBase* pTriangleFilter) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Sunshade)->checkStrikeLine(rStart, rDirection, count, pPartsFilter, pTriangleFilter);
    }

    const HitInfo* getStrikeInfoMap(u32 index) {
        return MR::getCollisionDirector()->getCategoryKeeper(CollisionCategory_Map)->getStrikeInfo(index);
    }

    u32 getStrikeInfoNumMap() {
        return ::getStrikeInfoNumCategory(0);
    }
};  // namespace Collision
