#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class LiveActor;
class MapPartsRailMover;

class DivideMercatorRailPosInfo {
public:
    inline DivideMercatorRailPosInfo() {
    }

    virtual void setPosition(s32, const Vec3f&) = 0;
};

class MercatorTransformCube : public AreaObj {
public:
    MercatorTransformCube(int, const char*);

    virtual void init(const TableIter&);

    void convertTransAndRotate(Vec3f*, Pos3f*, const Vec3f&, bool) const;
    f32 getSphereRadius() const;
    f32 calcLocalHeight(const Vec3f& rPos) const;
    void calcUV(Vec2f*, const Vec3f&) const;
    void calcLocalBoxSize(Vec3f*) const;

    Mtx34f mPlacementMtx;
};

namespace MR {
    void convertMercatorPlaneToSphereTransAndRotate(Vec3f*, Pos3f*, const Vec3f&, bool);
    void convertMercatorPlaneToSphereTransAndRotate(Vec3f*, Vec3f*, const Vec3f&, bool);
    void initDefaultPosForMercator(LiveActor*, const TableIter&, bool);
    void calcNearestRailPosForMercator(Vec3f*, const LiveActor*, f32);
    void calcRailClippingInfoForMercator(Vec3f*, f32*, LiveActor*, f32, f32);
    void initAndSetRailClippingForMercator(Vec3f*, LiveActor*, f32, f32);
    f32 calcRailTotalLengthForMercator(const LiveActor*, f32);
    void getDivideMercatorRailPosition(DivideMercatorRailPosInfo*, const LiveActor*, u32, f32, u32);
    MapPartsRailMover* createMapPartsRailMoverForMercator(LiveActor*, const TableIter&, bool);
};  // namespace MR
