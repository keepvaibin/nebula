#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class MapPartsRotator;

class LavaSunPlanet : public LiveActor {
public:
    /// @brief Creates a new `LavaSunPlanet`.
    /// @param pName A pointer to the null-terminated name of the object.
    LavaSunPlanet(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    MapPartsRotator* mRotator;
    const char* mObjName;
    LodCtrl* mLodCtrl;
};
