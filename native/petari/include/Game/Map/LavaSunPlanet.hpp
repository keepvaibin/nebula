#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class MapPartsRotator;

class LavaSunPlanet : public LiveActor {
public:
    LavaSunPlanet(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    MapPartsRotator* mRotator;
    const char* mObjName;
    LodCtrl* mLodCtrl;
};
