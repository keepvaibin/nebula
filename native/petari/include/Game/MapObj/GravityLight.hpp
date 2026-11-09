#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LightCylinder;
class MapPartsRailMover;
class MapPartsRotator;

class GravityLight : public LiveActor {
public:
    GravityLight(const char* pName);

    virtual ~GravityLight() override {
    }

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }

    void exeWait() {
    }

    Mtx34f mBaseMtx;
    LightCylinder* mLightCylinder;
    MapPartsRailMover* mMapPartsRailMover;
    MapPartsRotator* mMapPartsRotator;
    f32 mRadius;
    Vec3f mCenter;
};
