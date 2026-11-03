#pragma once

#include <JSystem/JGeometry.hpp>
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

    TVec3f getTrans();
    void init(const TPos3f&, HitSensor*, const void*, const void*, s32, bool);
    // A collision model the game built (DynamicCollisionObj), not a file.
    void init(const TPos3f&, HitSensor*, KCLFile*, const void*, s32, bool);
    void addToBelongZone();
    void removeFromBelongZone();
    void initWithAutoEqualScale(const TPos3f&, HitSensor*, const void*, const void*, s32, bool);
    void initWithNotUsingScale(const TPos3f&, HitSensor*, const void*, const void*, s32, bool);
    void resetAllMtx(const TPos3f&);
    void resetAllMtx();
    void forceResetAllMtxAndSetUpdateMtxOneTime();
    void resetAllMtxPrivate(const TPos3f&);
    void setMtx(const TPos3f&);
    void setMtx();
    void updateMtx();
    f32 makeEqualScale(MtxPtr);
    void updateBoundingSphereRange();
    void updateBoundingSphereRange(TVec3f);
    void updateBoundingSphereRangePrivate(f32);
    const char* getHostName() const;
    s32 getPlacementZoneID() const;
    bool checkStrikePoint(HitInfo*, const TVec3f&);
    u32 checkStrikeBall(HitInfo*, u32, const TVec3f&, f32, bool, const TriangleFilterBase*);
    u32 checkStrikeBallCore(HitInfo*, u32, const TVec3f&, const TVec3f&, f32, f32, f32, KC_PrismData**, f32*, u8*, const TriangleFilterBase*,
                            const TVec3f*);
    u32 checkStrikeBallWithThickness(HitInfo*, u32, const TVec3f&, f32, f32, const TriangleFilterBase*);
    void calcCollidePosition(TVec3f*, const KC_PrismData&, u8);
    void projectToPlane(TVec3f*, const TVec3f&, const TVec3f&, const TVec3f&);
    u32 checkStrikeLine(HitInfo*, u32, const TVec3f&, const TVec3f&, const TriangleFilterBase*);
    u32 createAreaPolygonList(Triangle*, u32, const TVec3f&, const TVec3f&);
    u32 createAreaPolygonListArray(Triangle*, u32, TVec3f*, u32);
    void calcForceMovePower(TVec3f*, const TVec3f&) const;

    TPos3f* _0;
    TPos3f mMatrix;
    TPos3f mBaseMatrix;
    TPos3f mInvBaseMatrix;
    TPos3f mPrevBaseMatrix;
    KCollisionServer* mServer;
    HitSensor* mHitSensor;
    bool _CC;
    bool _CD;
    bool _CE;
    bool _CF;
    bool _D0;
    u8 _D1[3];
    s32 _D4;
    f32 _D8;
    f32 _DC;
    s32 mKeeperIndex;
    CollisionZone* mZone;
    f32 _E8;
    f32 _EC;
    f32 _F0;
};
