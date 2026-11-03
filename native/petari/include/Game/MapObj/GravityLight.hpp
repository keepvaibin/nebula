#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class LightCylinder;
class MapPartsRailMover;
class MapPartsRotator;

class GravityLight : public LiveActor {
public:
    GravityLight(const char* pName);

    virtual ~GravityLight() {
    }

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mBaseMtx;
    }

    void exeWait() {
    }

    TMtx34f mBaseMtx;
    LightCylinder* mLightCylinder;
    MapPartsRailMover* mMapPartsRailMover;
    MapPartsRotator* mMapPartsRotator;
    f32 mRadius;
    TVec3f mCenter;
    bool _D8;
};
