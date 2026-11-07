#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LightCylinder;
class MapPartsRailMover;
class MapPartsRotator;

class GravityLight : public LiveActor {
public:
    GravityLight(const char* pName);

    virtual ~GravityLight() {
    }

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mBaseMtx;
    }

    void exeWait() {
    }

    Mtx34f mBaseMtx;
    LightCylinder* mLightCylinder;
    MapPartsRailMover* mMapPartsRailMover;
    MapPartsRotator* mMapPartsRotator;
    f32 mRadius;
    Vec3f mCenter;
    bool _D8;
};
