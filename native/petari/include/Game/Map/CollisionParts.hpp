#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class CollisionZone;
class HitInfo;
class HitSensor;
struct KCLFile;
struct KC_PrismData;
class KCollisionServer;
class Triangle;
class TriangleFilterBase;

class CollisionParts {
public:
    CollisionParts();

    Vec3f getTrans();
    void init(const Pos3f&, HitSensor*, const void*, const void*, s32, bool);
    // A collision model the game built (DynamicCollisionObj), not a file.
    void init(const Pos3f&, HitSensor*, KCLFile*, const void*, s32, bool);
    void addToBelongZone();
    void removeFromBelongZone();
    void initWithAutoEqualScale(const Pos3f&, HitSensor*, const void*, const void*, s32, bool);
    void initWithNotUsingScale(const Pos3f&, HitSensor*, const void*, const void*, s32, bool);
    void resetAllMtx(const Pos3f&);
    void resetAllMtx();
    void forceResetAllMtxAndSetUpdateMtxOneTime();
    void resetAllMtxPrivate(const Pos3f&);
    void setMtx(const Pos3f&);
    void setMtx();
    void updateMtx();
    f32 makeEqualScale(Pos3f&);
    void updateBoundingSphereRange();
    void updateBoundingSphereRange(Vec3f);
    void updateBoundingSphereRangePrivate(f32);
    const char* getHostName() const;
    s32 getPlacementZoneID() const;
    bool checkStrikePoint(HitInfo*, const Vec3f&);
    u32 checkStrikeBall(HitInfo*, u32, const Vec3f&, f32, bool, const TriangleFilterBase*);
    u32 checkStrikeBallCore(HitInfo*, u32, const Vec3f&, const Vec3f&, f32, f32, f32, KC_PrismData**, f32*, u8*, const TriangleFilterBase*,
                            const Vec3f*);
    u32 checkStrikeBallWithThickness(HitInfo*, u32, const Vec3f&, f32, f32, const TriangleFilterBase*);
    void calcCollidePosition(Vec3f*, const KC_PrismData&, u8);
    void projectToPlane(Vec3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    u32 checkStrikeLine(HitInfo*, u32, const Vec3f&, const Vec3f&, const TriangleFilterBase*);
    u32 createAreaPolygonList(Triangle*, u32, const Vec3f&, const Vec3f&);
    u32 createAreaPolygonListArray(Triangle*, u32, Vec3f*, u32);
    void calcForceMovePower(Vec3f*, const Vec3f&) const;

    MtxPtr mFollowMtx;  // the host's matrix the parts follow, or null
    Pos3f mMatrix;
    Pos3f mBaseMatrix;
    Pos3f mInvBaseMatrix;
    Pos3f mPrevBaseMatrix;
    KCollisionServer* mServer;
    HitSensor* mHitSensor;
    bool mIsValid;
    bool mIsUpdate;
    bool mIsUpdateOnce;
    bool mUseEqualScale;
    bool mIsNotUsingScale;
    s32 mStillCount;
    f32 mBoundingRadius;
    f32 mScale;
    s32 mKeeperIndex;
    CollisionZone* mZone;
};
