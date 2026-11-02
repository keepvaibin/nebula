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

    virtual ~CollisionCategorizedKeeper();
    virtual void movement();

    void addToZone(CollisionParts*, s32);
    void removeFromZone(CollisionParts*, s32);
    void addToGlobal(CollisionParts*);
    void removeFromGlobal(CollisionParts*);

    s32 checkStrikePoint(const TVec3f&, HitInfo*);
    s32 checkStrikeBall(const TVec3f&, f32, bool, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeBallWithThickness(const TVec3f&, f32, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    s32 checkStrikeLine(const TVec3f&, const TVec3f&, s32, const CollisionPartsFilterBase*, const TriangleFilterBase*);
    u32 createAreaPolygonList(Triangle*, u32, const TVec3f&, const TVec3f&);
    u32 createAreaPolygonListArray(Triangle*, u32, TVec3f*, u32);
    bool isSphereOverlappingWithBox(const TVec3f&, const TVec3f&, const TVec3f&, f32);
    bool searchSameHostParts(CollisionParts**, CollisionParts*) const;
    HitInfo* getStrikeInfo(u32);
    CollisionZone* getZone(int);

    HitInfo* mHitInfoArray;
    s32 _10;
    s32 mPartsCount;
    MR::Vector< MR::FixedArray< CollisionZone*, 32 > > mZones;
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
    void addAndUpdateMinMax(TVec3f, TVec3f);
    void eraseParts(CollisionParts*);

    s32 mZoneID;
    MR::Vector< MR::FixedArray< CollisionParts*, 512 > > mParts;
    TVec3f mCenter;
    f32 mRadius;
    TVec3f mMin;
    TVec3f mMax;
};
