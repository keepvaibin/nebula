#pragma once

#include <nebula_geometry.hpp>
#include "Game/LumaMarks.hpp"

class CollisionPartsFilterBase;
class HitInfo;
class HitSensor;
class TableIter;
class LiveActor;
class Triangle;
class TriangleFilterBase;

namespace MR {
    const Vec3f* getNormal(const Triangle*);
    bool isWallPolygon(const Vec3f&, const Vec3f&);
    bool isFloorPolygon(const Vec3f&, const Vec3f&);
    bool isFloorPolygonCos(const Vec3f&, const Vec3f&, f32);
    bool isWallPolygon(f32);
    bool isFloorPolygon(f32);
    bool isCeilingPolygon(f32);
    bool isWaterPolygon(const Triangle*);
    bool isThroughPolygon(const Triangle*);
    bool getFirstPolyOnLineToMap(LUMA_OUT LUMA_NULLABLE Vec3f*, Triangle*, const Vec3f&, const Vec3f&);
    bool getFirstPolyOnLineToMapAndMoveLimit(Vec3f*, Triangle*, const Vec3f&, const Vec3f&);
    bool getFirstPolyOnLineToWaterSurface(Vec3f*, Triangle*, const Vec3f&, const Vec3f&);
    bool getFirstPolyOnLineToMapExceptSensor(Vec3f*, Triangle*, const Vec3f&, const Vec3f&, const HitSensor*);
    bool getFirstPolyOnLineToMapExceptActor(Vec3f*, Triangle*, const Vec3f&, const Vec3f&, const LiveActor*);
    bool getFirstPolyOnLineToMap(LUMA_OUT LUMA_NULLABLE Vec3f*, Triangle*, const Vec3f&, const Vec3f&, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    bool getFirstPolyOnLineToWaterSurface(Vec3f*, Triangle*, const Vec3f&, const Vec3f&, const CollisionPartsFilterBase*,
                                          const TriangleFilterBase*);
    bool getFirstPolyNormalOnLineToMap(LUMA_OUT Vec3f*, const Vec3f&, const Vec3f&, LUMA_OUT Vec3f*, const HitSensor*);
    u32 getNearPolyOnLineSort(const Vec3f&, const Vec3f&, const Vec3f&, const HitSensor*);
    bool getSortedPoly(Vec3f*, Triangle*, u32);
    bool isExistMapCollision(const Vec3f&, const Vec3f&);
    bool isExistMoveLimitCollision(const Vec3f&, const Vec3f&);
    bool isExistMapCollisionExceptActor(const Vec3f&, const Vec3f&, const LiveActor*);
    bool checkStrikePointToMap(const Vec3f&, HitInfo*);
    bool checkStrikeBallToMap(const Vec3f&, f32);
    bool calcMapGround(const Vec3f&, Vec3f*, f32);
    bool calcMapGroundUpper(Vec3f*, const LiveActor*);
    bool isFallNextMove(const LiveActor*, f32, f32, f32, const TriangleFilterBase*);
    bool isFallNextMove(const Vec3f&, const Vec3f&, const Vec3f&, f32, f32, f32, const TriangleFilterBase*);
    bool isFallOrDangerNextMove(const LiveActor*, f32, f32, f32);
    bool isFallOrDangerNextMove(const Vec3f&, const Vec3f&, const Vec3f&, f32, f32, f32);
    void calcVelocityMovingPoint(const Triangle*, const Vec3f&, Vec3f*);
    u32 createAreaPolygonList(Triangle*, u32, const Vec3f&, const Vec3f&);
    u32 createAreaPolygonListArray(Triangle*, u32, Vec3f*, u32);
    bool trySetMoveLimitCollision(LiveActor*);
    bool isBindedGroundIce(const LiveActor*);
    bool isBindedGroundSand(const LiveActor*);
    bool isBindedGroundDamageFire(const LiveActor*);
    bool isBindedGroundWaterBottomH(const LiveActor*);
    bool isBindedGroundWaterBottomM(const LiveActor*);
    bool isBindedGroundWater(const LiveActor*);
    bool isBindedGroundSinkDeath(const LiveActor*);
    bool isBindedGroundAreaMove(const LiveActor*);
    bool isBindedGroundRailMove(const LiveActor*);
    bool isBindedGroundBrake(const LiveActor*);
    bool isBindedDamageFire(const LiveActor*);
    bool isBindedDamageElectric(const LiveActor*);
    u32 getCameraID(const Triangle*);
    // A polygon's codes (CollisionFloorCode, CollisionWallCode, CollisionSoundCode); no attributes: 0.
    s32 getFloorCodeIndex(const TableIter&);
    s32 getSoundCodeIndex(const TableIter&);
    s32 getFloorCodeIndex(const Triangle*);
    s32 getWallCodeIndex(const Triangle*);
    s32 getSoundCodeIndex(const Triangle*);
    bool isGroundCodeWaterIter(const TableIter&);
    bool isGroundCodeDeath(const Triangle*);
    bool isGroundCodeDamage(const Triangle*);
    bool isGroundCodeIce(const Triangle*);
    bool isGroundCodeDamageFire(const Triangle*);
    bool isGroundCodeFireDance(const Triangle*);
    bool isGroundCodeSand(const Triangle*);
    bool isGroundCodeDamageElectric(const Triangle*);
    bool isGroundCodeWaterBottomH(const Triangle*);
    bool isGroundCodeWaterBottomM(const Triangle*);
    bool isGroundCodeSinkDeath(const Triangle*);
    bool isGroundCodeRailMove(const Triangle*);
    bool isGroundCodeAreaMove(const Triangle*);
    bool isGroundCodeNoStampSand(const Triangle*);
    bool isGroundCodeSinkDeathMud(const Triangle*);
    bool isGroundCodeBrake(const Triangle*);
    bool isWallCodeGhostThrough(const Triangle*);
    bool isWallCodeRebound(const Triangle*);
    bool isWallCodeNoAction(const Triangle*);
    bool isCameraCodeThrough(const Triangle*);
    bool isCodeSand(const Triangle*);
    bool getFirstPolyOnLineBFast(const Vec3f&, const Vec3f&, Vec3f*, Triangle*);
};  // namespace MR

namespace Collision {
    s32 checkStrikePointToMap(const Vec3f&, HitInfo*);
    s32 checkStrikeBallToMap(const Vec3f&, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeBallToMapWithMovingReaction(const Vec3f&, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeBallToMapWithThickness(const Vec3f&, f32, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeLineToMap(const Vec3f&, const Vec3f&, s32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeLineToSunshade(const Vec3f&, const Vec3f&, s32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    const HitInfo* getStrikeInfoMap(u32);
    u32 getStrikeInfoNumMap();
};  // namespace Collision
