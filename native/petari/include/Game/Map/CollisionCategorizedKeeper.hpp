#pragma once

#include "Game/Map/HitInfo.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class CollisionZone;
class CollisionParts;
class TriangleFilterBase;
class CollisionPartsFilterBase;

class CollisionCategorizedKeeper : public NameObj {
public:
    CollisionCategorizedKeeper(s32);

    virtual void movement();

    void addToZone(CollisionParts*, s32);
    void removeFromZone(CollisionParts*, s32);
    void addToGlobal(CollisionParts*);
    void removeFromGlobal(CollisionParts*);

    s32 checkStrikePoint(const Vec3f&, HitInfo*);
    s32 checkStrikeBall(const Vec3f&, f32, bool, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeBallWithThickness(const Vec3f&, f32, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeLine(const Vec3f&, const Vec3f&, s32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 collectStrikeBall(const Vec3f&, f32, bool, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 collectStrikeBallWithThickness(const Vec3f&, f32, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 collectStrikeLine(const Vec3f&, const Vec3f&, s32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    void growHitInfo();
    u32 createAreaPolygonList(Triangle*, u32, const Vec3f&, const Vec3f&);
    u32 createAreaPolygonListArray(Triangle*, u32, Vec3f*, u32);
    bool isSphereOverlappingWithBox(const Vec3f&, const Vec3f&, const Vec3f&, f32);
    bool searchSameHostParts(CollisionParts**, CollisionParts*) const;
    HitInfo* getStrikeInfo(u32);
    CollisionZone* getZone(int);

    HitInfo* mHitInfoArray;
    s32 mHitInfoRoom;
    s32 mNumHits;
    s32 mPartsCount;
    MR::InlineVector< CollisionZone*, 32 > mZones;
    s32 mCategory;
    bool mZonesInitialized;
    bool mUpdateZoneBounds;
};

class CollisionZone {
public:
    CollisionZone(s32);

    void addParts(CollisionParts*);
    void calcMinMaxAndRadius();
    void calcMinMaxAndRadiusIfMoveOuter(CollisionParts*);
    void addAndUpdateMinMax(Vec3f, Vec3f);
    void eraseParts(CollisionParts*);

    s32 mZoneID;
    MR::InlineVector< CollisionParts*, 512 > mParts;
    Vec3f mCenter;
    f32 mRadius;
    Vec3f mMin;
    Vec3f mMax;
};
