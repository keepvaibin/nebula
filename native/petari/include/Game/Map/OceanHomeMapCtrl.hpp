#pragma once

#include "Game/NameObj/NameObj.hpp"

class PlanetMap;
class ModelObj;

class OceanHomeMapCtrl : public NameObj {
public:
    OceanHomeMapCtrl();

    virtual ~OceanHomeMapCtrl();
    virtual void init(const JMapInfoIter&);
    virtual void movement();

    void entryMapRing(PlanetMap*);

    PlanetMap* mOceanHomePlanet;           // Referencing an removed object?
    PlanetMap* mOceanRingPlanet;           // OceanRingPlanet
    u32 _14;                               // Unused variable
    ModelObj* mOceanRingPlanetLowInWater;  // OceanRingPlanetLowInWater
    u32 _1C;                               // Unused variable
    u32 _20;                               // Unused variable
};

namespace OceanHomeMapFunction {
    void tryEntryOceanHomeMap(PlanetMap*);
};  // namespace OceanHomeMapFunction
