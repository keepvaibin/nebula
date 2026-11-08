#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class MapPartsRotator;

class LavaSunPlanet : public LiveActor {
public:
    LavaSunPlanet(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    MapPartsRotator* mRotator;
    const char* mObjName;
    LodCtrl* mLodCtrl;
};
